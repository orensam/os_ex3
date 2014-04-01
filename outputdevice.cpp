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


using namespace std;

typedef struct
{
	char* buff;
	unsigned long int length;
	//bool wasWritten;
} Task;

static const int CODE_ISNT_CLOSING = -2;
static const int CODE_INVALID_TASK_ID = -2;
static const int CODE_FILESYSTEM_ERROR = -2;
static const int CODE_SUCCESS = 0;
static const int CODE_FLUSH_SUCCESS = 1;
static const int CODE_WAIT4CLOSE_SUCCESS = 1;
static const int CODE_FAILURE = -1;

static const string ERROR_SYSCALL = "system error";
static const string ERROR_GENERAL = "Output device library error";


static pthread_mutex_t mut;
// Under mutex:
static vector<Task*> taskVec;
static queue<int> writeQueue;
static set<int> freeIds;

static FILE * outFile;
static int taskCount;
static bool isClosing = false;
static bool isInited = false;

static pthread_cond_t freeIdsCond = PTHREAD_COND_INITIALIZER;
static pthread_cond_t queueCond = PTHREAD_COND_INITIALIZER;
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
	t->buff = new char[strlen(buffer) + 1];
	strcpy(t->buff, buffer);
	return t;
}

void destroyTask(Task * t)
{
	delete[] t->buff;
	delete t;
}

/**
 * Writer thread function.
 * Handles the actual writing of the tasks to the device, in a non-blocking fashion.
 */
void* threadRun(void* ptr = NULL)
{
	while(true)
	{
		// If the device is closing, and the queue is empty -
		// We finished writing all the tasks, so the thread finished its jib.
		pthread_mutex_lock(&mut);
		if (writeQueue.empty() && isClosing)
		{
			cout << "threadRun: queue empty, and is closing " << endl;
			pthread_mutex_unlock(&mut);
			break;
		}

		// Wait until there is a task that needs to be written
		if(writeQueue.empty())
		{
			pthread_cond_wait(&queueCond, &mut);
		}

		cout << "threadRun: something in queue!" << endl;

		// Tasks exist in queue, write the first one in line
		int id = writeQueue.front();
		cout << "threadRun: trying to write id " << id << ", vec size is " <<  taskVec.size() << endl;
		writeQueue.pop();
		Task* t = taskVec[id];

		cout << "threadRun: got task with buffer " << t->buff << endl;

		pthread_mutex_unlock(&mut);

		// Release lock and write the task
		fwrite(t->buff, sizeof(char), t->length, outFile);
		taskCount++; // TODO: Handle integer overflow
		destroyTask(t);

		// Now acquire lock, add the task to the available IDs set, and signal (for flush2device)
		pthread_mutex_lock(&mut);

		freeIds.insert(id);
		pthread_cond_broadcast(&freeIdsCond);

		pthread_mutex_unlock(&mut);
//		cout << "threadRun: END LOOP" << endl;
	}
	cout << "threadRun: after loop " << endl;
	isClosing = false;
	fclose(outFile);
	//pthread_exit(ptr); // TODO: what to return?
	return ptr;
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
	if (isClosing)
	{
		// Device closing - no writing permitted.
		error(ERROR_GENERAL);
		return CODE_FAILURE;
	}

	if (isInited)
	{
		// Already initialized - do not init again
		error(ERROR_GENERAL);
		return CODE_FAILURE;
	}

	pthread_mutex_init(&mut, NULL);
	isClosing = false;
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
	isInited = true;
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
	cout << "write2device: created task with buffer " << t->buff << endl;
	t->length = length;

	// Get task ID
	int id;
	pthread_mutex_lock(&mut);
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

//	pthread_mutex_unlock(&mut);
//	pthread_mutex_lock(&mut);

	// Add to queue
	writeQueue.push(id);
	// Signal new task
	pthread_cond_broadcast(&queueCond);

	pthread_mutex_unlock(&mut);
//	cout << "write2device: END" << endl;
	return id;
}



/**
 * Returns whether or not the given task_id has ever existed in the system.
 */
bool taskExists(int task_id)
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

	pthread_mutex_lock(&mut);
	while(freeIds.find(task_id) == freeIds.end())
	{
	    pthread_cond_wait(&freeIdsCond, &mut);
	}
	pthread_mutex_unlock(&mut);

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
	return (freeIds.find(task_id) != freeIds.end());
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
	isClosing = true;
	isInited = false;
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

	pthread_mutex_lock(&mut);
	while(!writeQueue.empty())
	{
		// Wait until a task was written,
		// Then check again if there's anything in the queue.
		pthread_cond_wait(&freeIdsCond, &mut);
	}
	pthread_mutex_unlock(&mut);

	return CODE_WAIT4CLOSE_SUCCESS;
}





