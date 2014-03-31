/*
 * outputdevice.cpp
 *
 *       A Multi-Threaded Output Device Library
 */

#include <string>
#include <iostream>
#include <vector>
#include <queue>

using namespace std;

typedef struct
{
	int taskId;
	char* buff;
} Task;

static const int CODE_INVALID_TASK_ID = -2;
static const int CODE_FILESYSTEM_ERROR = -2;
static const int CODE_SUCCESS = 0;
static const int CODE_FLUSH_SUCCESS = 0;
static const int CODE_FAILURE = -1;

static const string ERROR_SYSCALL = "system error";
static const string ERROR_GENERAL = "Output device library error";

static int taskCount;
static vector<Task> taskVec;
static queue<int> writeQueue;
int nextTaskId = 0;

/**
 * Writes the given error message to stderr
 */
void error(const string& msg)
{
	cerr << msg << endl;
}

/**
 * DESCRIPTION: The function creates the file filename if it does not exist and open the file for writing.
 *              This function should be called prior to any other functions as a necessary precondition for their success.
 *
 * RETURN VALUE: On success 0, -2 for a filesystem error (inability to create the file, etc.), otherwise -1.
 */
int initdevice(char* filename)
{
	taskCount = 0;
}

/**
 * DESCRIPTION: The function writes the input buffer to the file. The buffer may be freed once this call returns.
 *              You should deal with any memory management issues.
 * 		Note this is non-blocking package you are required to implement you should return ASAP,
 * 		even if the buffer has not yet been written to the disk.
 *
 * RETURN VALUE: On success, the function returns a task_id (>= 0), which identifies this write operation.
 * 		 Note, you should reuse task_ids when they become available.  On failure, -1 will be returned.
 */
int write2device(char* buffer, int length)
{
}

/**
 * DESCRIPTION: Block until the specified task_id has been written to the file.
 * 		The task_id is a value that was previously returned by write2device function.
 * 		In case of task_id doesn't exist, should return -2; In case of other errors, return -1.
 */
int flush2device(int task_id)
{
}

/**
 * DESCRIPTION: return (withoug blocking) whether the specified task_id has been written to the file
 *      (0 if yes, 1 if not).
 * 		The task_id is a value that was previously returned by write2device function.
 * 		In case of task_id doesn't exist, should return -2; In case of other errors, return -1.
 */
int wasItWritten(int task_id)
{
}

/**
 * DESCRIPTION: return (withoug blocking) how many tasks have been written to file since last initdevice.
 *      If number exceeds MAX_INT, return MIN_INT.
 * 		In case of error, return -1.
 */
int howManyWritten()
{
}

/**
 * DESCRIPTION: close the output file and reset the system so that it is possible to call initdevice again.
 *              All pending task_ids should be written to output disk file.
 *              Any attempt to write new buffers (or initialize) while the system is shutting down should
 *              cause an error.
 *              In case of error, the function should cause the process to exit.
 */
void closedevice()
{
}

/**
 * DESCRIPTION: Wait for closedevice to finish.
 *              If closing was successful, it returns 1.
 *              If closedevice was not called it should return -2.
 *              In case of other error, it should return -1.
 *
 */
int wait4close()
{
}





