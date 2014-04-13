/*
 * outputdevice.cpp
 *
 *       A Multi-Threaded Output Device Library
 */

#include <string>
#include <iostream>
#include <vector>
#include <queue>
#include <set>
#include <cstring>
#include <cstdio>
#include <pthread.h>
#include <limits.h>

#include <unistd.h>

using namespace std;

typedef struct
{
	char* buff;
	unsigned long int length;
} Task;

static const int CODE_ISNT_CLOSING = -2;
static const int CODE_INVALID_TASK_ID = -2;
static const int CODE_FILESYSTEM_ERROR = -2;
static const int CODE_SUCCESS = 0;
static const int CODE_TASK_WRITTEN = 0;
static const int CODE_TASK_NOT_WRITTEN = 1;
static const int CODE_FLUSH_SUCCESS = 1;
static const int CODE_WAIT4CLOSE_SUCCESS = 1;
static const int CODE_FAILURE = -1;

static const string ERROR_SYSCALL = "system error";
static const string ERROR_GENERAL = "Output device library error";


static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
// Under mutex:
static vector<Task*> taskVec;
static queue<int> writeQueue;
static set<int> freeIds;
static bool isClosing = false;
static bool isInited = false;

static FILE * outFile;
static int taskCount;

static pthread_cond_t signalTaskWritten = PTHREAD_COND_INITIALIZER;
static pthread_cond_t signalAttemptWrite = PTHREAD_COND_INITIALIZER;
static pthread_t writeThread;

/**
 * Writes the given error message to stderr
 */
void error(const string& msg)
{
	cerr << msg << endl;
}

/**
 * Creates a new task from the given buffer and length.
 */
Task* createTask(char* buffer, int length)
{
	Task * t = new Task();
	t->length = length;
	t->buff = new char[length];
	memcpy(t->buff, buffer, length);
	return t;
}

void destroyTask(Task * t)
{
	delete[] t->buff;
	delete t;
}

void incTaskCount()
{
	if (taskCount == INT_MAX)
	{
		taskCount = INT_MIN;
	}
	else
	{
		++taskCount;
	}
}
/**
 * Writer thread function.
 * Handles the actual writing of the tasks to the device, in a non-blocking fashion.
 */
void* threadRun(void* ptr = NULL)
{
	while(true)
	{
//		cout << "threadRun: start loop " << endl;
//		cout << "threadRun: queue empty? " << writeQueue.empty() << endl;
//		cout << "threadRun: isClosing? " << isClosing << endl;

		pthread_mutex_lock(&lock);

		// Wait until there is a task that needs to be written
		if(writeQueue.empty() && !isClosing)
		{
//			cout << "threadRun: waiting for task to be added" << endl;
			pthread_cond_wait(&signalAttemptWrite, &lock);
//			cout << "threadRun: received signalAttemptWrite. queue.empty() is " << writeQueue.empty() << " and isClosing is: " << isClosing << endl;
		}

		// If the device is closing, and the queue is empty -
		// We finished writing all the tasks, so the thread finished its jib.
		if (writeQueue.empty() && isClosing)
		{
//			cout << "threadRun: queue empty, and is closing " << endl;
			pthread_mutex_unlock(&lock);
			break;
		}

//		cout << "threadRun: something in queue! Queue size is: " << writeQueue.size() << endl;

		// Tasks exist in queue, write the first one in line
		int id = writeQueue.front();
		writeQueue.pop();
		Task* t = taskVec[id];
//		cout << "threadRun: popped id " << id << ", queue size is " <<  writeQueue.size() << endl;

//		cout << "threadRun: got task with buffer " << t->buff << endl;

		pthread_mutex_unlock(&lock);

		// Lock released, write the task
		cout << "threadRun: writing task id  " << id << endl;
		fwrite(t->buff, sizeof(char), t->length, outFile); //TODO: check for errors
		incTaskCount();

		// Now acquire lock, add the task to the available IDs set, and signal (for flush2device)
		pthread_mutex_lock(&lock);
		destroyTask(t);
		freeIds.insert(id);
		pthread_mutex_unlock(&lock);

		pthread_cond_broadcast(&signalTaskWritten);
//		cout << "threadRun: END LOOP" << endl;
	}
	cout << "threadRun: after loop " << endl;

	pthread_mutex_lock(&lock);
	isClosing = false;
	pthread_mutex_unlock(&lock);

	fclose(outFile);
	pthread_exit(ptr);
}

/**
 * The function creates the file filename if it does not exist and open the file for writing.
 * This function should be called prior to any other functions as a necessary precondition
 * for their success.
 *
 * RETURN VALUE: On success 0, -2 for a filesystem error (inability to create the file, etc.),
 * otherwise -1.
 */
int initdevice(char* filename)
{
	pthread_mutex_lock(&lock);

	// Already initialized - do not init again
	if (isInited)
	{
		pthread_mutex_unlock(&lock);
		return CODE_FAILURE;
	}

	// Device closing - no writing permitted.
	if (isClosing)
	{
		pthread_mutex_unlock(&lock);
		return CODE_FAILURE;
	}

	isInited = true;
	isClosing = false;

	pthread_mutex_unlock(&lock);

	// Initialize task counter
	taskCount = 0;
	outFile = fopen(filename, "ab");
	if (!outFile)
	{
		error(ERROR_GENERAL);
		return CODE_FILESYSTEM_ERROR;
	}
	if (pthread_create(&writeThread, NULL, &threadRun, NULL) != CODE_SUCCESS)
	{
		error(ERROR_GENERAL);
		return CODE_FAILURE;
	}
	return CODE_SUCCESS;
}

/**
 * The function writes the input buffer to the file. The buffer may be freed once this call returns.
 * You should deal with any memory management issues.
 * Note this is non-blocking package you are required to implement you should return ASAP,
 * even if the buffer has not yet been written to the disk.
 *
 * RETURN VALUE: On success, the function returns a task_id (>= 0),
 * which identifies this write operation.
 * Note, you should reuse task_ids when they become available.  On failure, -1 will be returned.
 */
int write2device(char* buffer, int length)
{
	if (isClosing)
	{
		// Device closing - no writing permitted.
		error(ERROR_GENERAL);
		return CODE_FAILURE;
	}

	// Create a new task
	Task * t = createTask(buffer, length);
	//usleep(100);

	// Get task ID
	int id;
	pthread_mutex_lock(&lock);
	if (freeIds.empty())
	{
		id = taskVec.size();
		taskVec.push_back(t);
	}
	else
	{
		set<int>::iterator it = freeIds.begin();
		id = *it;
		freeIds.erase(it);
		taskVec[id] = t;
	}

	// Add to queue
	writeQueue.push(id);
	cout << "write2device: created task id  " << id << endl;
	// Signal new task
	pthread_cond_broadcast(&signalAttemptWrite);
	pthread_mutex_unlock(&lock);


	//	cout << "write2device: END" << endl;
	return id;
}



/**
 * Returns whether or not the given task_id has ever existed in the system.
 */
bool taskExists(unsigned int task_id)
{
	return task_id >= 0 && task_id < taskVec.size();
}
/**
 * Block until the specified task_id has been written to the file.
 * The task_id is a value that was previously returned by write2device function.
 * In case of task_id doesn't exist, should return -2; In case of other errors, return -1.
 */
int flush2device(int task_id)
{
	if (!taskExists(task_id))
	{
		return CODE_INVALID_TASK_ID;
	}

	pthread_mutex_lock(&lock);
	while(freeIds.find(task_id) == freeIds.end())
	{
	    pthread_cond_wait(&signalTaskWritten, &lock);
	}
	pthread_mutex_unlock(&lock);

	return CODE_FLUSH_SUCCESS;
}

/**
 * Return (withoug blocking) whether the specified task_id has been written to the file
 * (0 if yes, 1 if not).
 * The task_id is a value that was previously returned by write2device function.
 * In case of task_id doesn't exist, should return -2; In case of other errors, return -1.
 */
int wasItWritten(int task_id)
{
	if (!taskExists(task_id))
	{
		return CODE_INVALID_TASK_ID;
	}
	if (freeIds.find(task_id) != freeIds.end())
	{
		return CODE_TASK_WRITTEN;
	}
	return CODE_TASK_NOT_WRITTEN;
}


/**
 * Return (withoug blocking) how many tasks have been written to file since last initdevice.
 * If number exceeds MAX_INT, return MIN_INT.
 * In case of error, return -1.
 */
int howManyWritten()
{
	return taskCount;
}

/**
 * close the output file and reset the system so that it is possible to call initdevice again.
 * All pending task_ids should be written to output disk file.
 * Any attempt to write new buffers (or initialize) while the system is shutting down should
 * cause an error.
 * In case of error, the function should cause the process to exit.
 */
void closedevice()
{
	cout << "closedevice() start" << endl;
	pthread_mutex_lock(&lock);
	isClosing = true;
	isInited = false;
	pthread_cond_broadcast(&signalAttemptWrite);
	pthread_mutex_unlock(&lock);
}

/**
 * Wait for closedevice to finish.
 * If closing was successful, it returns 1.
 * If closedevice was not called it should return -2.
 * In case of other error, it should return -1.
 */
int wait4close()
{
	if (!isClosing)
	{
		return CODE_ISNT_CLOSING;
	}
	pthread_join(writeThread, NULL);

	return CODE_WAIT4CLOSE_SUCCESS;
}





