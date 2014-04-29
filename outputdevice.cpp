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
#include <stdlib.h>

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

// Macros for locking/unlocking a mutex, and failing nicely if needed.
#define LOCK(x) if (pthread_mutex_lock(x)) {error(ERROR_GENERAL); return CODE_FAILURE;}
#define UNLOCK(x) if (pthread_mutex_unlock(x)) {error(ERROR_GENERAL); return CODE_FAILURE;}
#define LOCK_OR_DIE(x) if (pthread_mutex_lock(x)) {error(ERROR_GENERAL); dieNicely();}
#define UNLOCK_OR_DIE(x) if (pthread_mutex_unlock(x)) {error(ERROR_GENERAL); dieNicely();}

// static const string ERROR_SYSCALL = "system error"; // Used nowhere.
static const string ERROR_GENERAL = "Output device library error";

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

// Under mutex:
static vector<Task*> taskVec;
static queue<int> writeQueue;
static set<int> freeIds;
static bool isClosing = false;
static bool isInited = false;
static bool everInited = false;

static FILE * outFile;
static int taskCount;

// Condition Variables
static pthread_cond_t signalTaskWritten = PTHREAD_COND_INITIALIZER;
static pthread_cond_t signalAttemptWrite = PTHREAD_COND_INITIALIZER;
static pthread_cond_t signalThreadEnded = PTHREAD_COND_INITIALIZER;

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

/**
 * Creates the library's condition variables.
 */
int createCVs()
{
	if (pthread_cond_init(&signalAttemptWrite, NULL))
	{
		return CODE_FAILURE;
	}
	if (pthread_cond_init(&signalTaskWritten, NULL))
	{
		pthread_cond_destroy(&signalAttemptWrite);
		return CODE_FAILURE;
	}
	if	(pthread_cond_init(&signalThreadEnded, NULL))
	{
		pthread_cond_destroy(&signalAttemptWrite);
		pthread_cond_destroy(&signalTaskWritten);
		return CODE_FAILURE;
	}
	return CODE_SUCCESS;
}

/**
 * Deletes the library's condition variables.
 */
void deleteCVs()
{
	pthread_cond_destroy(&signalAttemptWrite);
	pthread_cond_destroy(&signalTaskWritten);
	pthread_cond_destroy(&signalThreadEnded);
}

/**
 * Destroys the task pointed to by t.
 */
void destroyTask(Task * t)
{
	delete[] t->buff;
	delete t;
}

/**
 * Frees the dynamically-allocated memory and kills the whole process.
 * Meant for use in background-thread errors and in closedevice().
 */
void dieNicely()
{
	// Delete all tasks
	vector<Task*>::iterator it = taskVec.begin();
	for (; it != taskVec.end(); ++it)
	{
		destroyTask(*it);
	}
	// Delete condition variables
	deleteCVs();
	exit(CODE_FAILURE);
}

/**
 * Increments the total number of tasks.
 * if it reached maxint, returns minint.
 */
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
		LOCK_OR_DIE(&lock);

		// Wait until there is a task that needs to be written
		if(writeQueue.empty() && !isClosing)
		{
			pthread_cond_wait(&signalAttemptWrite, &lock);
		}

		// If the device is closing, and the queue is empty -
		// We finished writing all the tasks, so the thread finished its jib.
		if (writeQueue.empty() && isClosing)
		{
			UNLOCK_OR_DIE(&lock);
			break;
		}

		// Tasks exist in queue, write the first one in line
		int id = writeQueue.front();
		writeQueue.pop();
		Task* t = taskVec[id];

		UNLOCK_OR_DIE(&lock);

		// Lock released, write the task
		if (fwrite(t->buff, sizeof(char), t->length, outFile) < t->length)
		{
			dieNicely();
		}
		incTaskCount();

		// Now acquire lock, add the task to the available IDs set, and signal (for flush2device)
		LOCK_OR_DIE(&lock);
		destroyTask(t);
		freeIds.insert(id);
		UNLOCK_OR_DIE(&lock);
		pthread_cond_broadcast(&signalTaskWritten);
	}

	// Cleanup

	if (fclose(outFile))
	{
		dieNicely();
	}

	LOCK_OR_DIE(&lock);

	pthread_cond_broadcast(&signalThreadEnded);
	deleteCVs();

	isClosing = false;
	UNLOCK_OR_DIE(&lock);

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
	LOCK(&lock);

	// Device closing / already initialized / CVs were not created - No initilization.
	if (isInited || isClosing || createCVs() != CODE_SUCCESS)
	{
		error(ERROR_GENERAL);
		UNLOCK(&lock);
		return CODE_FAILURE;
	}

	// Initialize task counter
	taskCount = 0;
	outFile = fopen(filename, "ab");
	if (!outFile)
	{
		error(ERROR_GENERAL);
		UNLOCK(&lock);
		return CODE_FILESYSTEM_ERROR;
	}
	if (pthread_create(&writeThread, NULL, &threadRun, NULL) != CODE_SUCCESS)
	{
		error(ERROR_GENERAL);
		UNLOCK(&lock);
		return CODE_FAILURE;
	}

	isInited = true;
	isClosing = false;
	everInited = true;

	UNLOCK(&lock);
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
	LOCK(&lock);
	if (isClosing || !isInited)
	{
		// Device closing or not initialized - no writing permitted.
		error(ERROR_GENERAL);
		UNLOCK(&lock);
		return CODE_FAILURE;
	}
	UNLOCK(&lock);

	// Create a new task
	Task * t = createTask(buffer, length);
	// Get task ID
	int id;

	LOCK(&lock);

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

	// Signal new task
	pthread_cond_broadcast(&signalAttemptWrite);
	UNLOCK(&lock);

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
	LOCK(&lock);
	// Task doesn't exist - return with error
	if (!taskExists(task_id))
	{
		error(ERROR_GENERAL);
		UNLOCK(&lock);
		return CODE_INVALID_TASK_ID;
	}

	while(freeIds.find(task_id) == freeIds.end())
	{
	    pthread_cond_wait(&signalTaskWritten, &lock);
	}
	UNLOCK(&lock);

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
	LOCK(&lock);
	if (!taskExists(task_id))
	{
		error(ERROR_GENERAL);
		UNLOCK(&lock);
		return CODE_INVALID_TASK_ID;
	}
	if (freeIds.find(task_id) != freeIds.end())
	{
		UNLOCK(&lock);
		return CODE_TASK_WRITTEN;
	}
	UNLOCK(&lock);
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
	LOCK_OR_DIE(&lock);
	if (!everInited)
	{
		UNLOCK_OR_DIE(&lock);
		error(ERROR_GENERAL);
		exit(CODE_FAILURE);
	}
	isClosing = true;
	isInited = false;
	pthread_cond_broadcast(&signalAttemptWrite);
	UNLOCK_OR_DIE(&lock);
}

/**
 * Wait for closedevice to finish.
 * If closing was successful, it returns 1.
 * If closedevice was not called it should return -2.
 * In case of other error, it should return -1.
 */
int wait4close()
{
	LOCK(&lock);

	// Already closed
	if (!isInited && !isClosing)
	{
		UNLOCK(&lock);
		return CODE_WAIT4CLOSE_SUCCESS;
	}

	// Error scenarios
	if (!isClosing || !everInited)
	{
		error(ERROR_GENERAL);
		UNLOCK(&lock);
		return CODE_ISNT_CLOSING;
	}

	// Currently closing - wait for thread death
	pthread_cond_wait(&signalThreadEnded, &lock);
	UNLOCK(&lock);

	return CODE_WAIT4CLOSE_SUCCESS;
}







