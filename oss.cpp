/********************************************
 * oss App - Process Scheduling (oss) Application
 * This is the main functionality of the oss
 * application.  It is called by the oss_main.cpp
 * file.
 * 
 * Brett Huffman
 * CMP SCI 4760 - Project 4
 * Due Mar 29, 2021
 * oss CPP file for oss project
 ********************************************/

#include <iostream>
#include <string.h>
#include <queue>
#include <list>
#include <algorithm>
#include <unistd.h>
#include "sharedStructures.h"
#include "productSemaphores.h"
#include "oss.h"
#include "bitmapper.h"
#include "deadlock.h"

using namespace std;

// SIGINT handling
volatile sig_atomic_t sigIntFlag = 0;
void sigintHandler(int sig){ // can be called asynchronously
  sigIntFlag = 1; // set flag
}

const int MAX_PROCESSES = 100;

// ossProcess - Process to start oss process.
int ossProcess(string strLogFile, bool VerboseMode)
{
    // Important items
    struct OssHeader* ossHeader;
    struct UserProcesses* ossUserProcesses;
    struct ResourceDescriptors* ossResourceDescriptors;
    int wstatus;
    long nNextTargetStartTime = 0;   // Next process' target start time

    // Queues for managing processes
    queue<int> readyQueue;
    list<int> blockedList;

    // Pid used throughout child
    const pid_t nPid = getpid();

    // Seed the randomizer with the PID
    srand(time(0) ^ nPid);

    // Start Time for time Analysis
    // Get the time in seconds for our process to make
    // sure we don't exceed the max amount of processing time
    time_t secondsStart = time(NULL);   // Start time
    int deadlockTimer = 1;
    struct tm * curtime = localtime( &secondsStart );   // Will use for filename uniqueness
    strLogFile.append("_").append(asctime(curtime));
    replace(strLogFile.begin(), strLogFile.end(), ' ', '_');
    replace(strLogFile.begin(), strLogFile.end(), '?', '_');

    // Print out the header
    LogItem("------------------------------------------------\n", strLogFile);
    LogItem("OSS by Brett Huffman - CMP SCI 4760 - Project 5\n", strLogFile);
    LogItem("------------------------------------------------\n", strLogFile);


    // Bitmap object for keeping track of children
    bitmapper bm(PROCESSES_MAX);

    // Register SIGINT handling
    signal(SIGINT, sigintHandler);
    bool isKilled = false;
    bool isShutdown = false;

    // Statistics
    int nProcessCount = 0;   // 100 MAX
    int nCPUProcessCount = 0;
    int nIOProcessCount = 0;
    uint nCPUTimeInSystem = 0;
    uint nIOTimeInSystem = 0;
    uint nCPU_CPUUtil = 0;
    uint nIO_CPUUtil = 0;
    uint nCPU_TimeWaitedBlocked = 0;
    uint nIO_TimeWaitedBlocked = 0;
    uint nTotalTime = 0;
    uint nCPU_IdleTime = 0;
    uint nTotalWaitTime = 0;

    // Create a Semaphore to coordinate control
    productSemaphores s(KEY_MUTEX, true, 1);
    if(!s.isInitialized())
    {
        perror("OSS: Could not successfully create Semaphore");
        exit(EXIT_FAILURE);
    }

    // Setup Message Queue Functionality
    // Note: The oss app will always have a type of 1
    int msgid = msgget(KEY_MESSAGE_QUEUE, IPC_CREAT | 0666); 
    if (msgid == -1) {
        perror("OSS: Error creating Message Queue");
        exit(EXIT_FAILURE);
    }

    // Setup shared memory
    // allocate a shared memory segment with size of 
    // Product Header + entire Product array
    int memSize = sizeof(struct OssHeader) + 
        (sizeof(struct UserProcesses) * PROCESSES_MAX) +
        (sizeof(struct ResourceDescriptors) * RESOURCES_MAX);
    shm_id = shmget(KEY_SHMEM, memSize, IPC_CREAT | IPC_EXCL | 0660);
    if (shm_id == -1) {
        perror("OSS: Error allocating shared memory");
        exit(EXIT_FAILURE);
    }

    // attach the shared memory segment to our process's address space
    shm_addr = (char*)shmat(shm_id, NULL, 0);
    if (!shm_addr) { /* operation failed. */
        perror("OSS: Error attaching shared memory");
        exit(EXIT_FAILURE);
    }

    // Get the queue header
    ossHeader = (struct OssHeader*) (shm_addr);
    // Get our entire queue
    ossUserProcesses = (struct UserProcesses*) (shm_addr+sizeof(struct OssHeader));
    // Get our entire queue
    ossResourceDescriptors = (struct ResourceDescriptors*) (ossUserProcesses+(sizeof(struct UserProcesses)*PROCESSES_MAX));

    // Index to Item Currently Processing - Start at nothing processing
    int nIndexToCurrentChildProcessing = -1;

    // Fill the product header
    ossHeader->simClockSeconds = 0;
    ossHeader->simClockNanoseconds = 0;

    // Zero-out all the arrays
    memset(ossHeader->availabilityMatrix, 0, sizeof(ossHeader->availabilityMatrix));
    memset(ossHeader->requestMatrix, 0, sizeof(ossHeader->requestMatrix));
    memset(ossHeader->allocatedMatrix, 0, sizeof(ossHeader->allocatedMatrix));

    // Setup all Descriptors per instructions
    for(int i=0; i < RESOURCES_MAX && !isShutdown; i++)
    {
        // First decide if > 1 item is allowed (20% of the time)
        if(getRandomProbability(0.20f))
        {
            // Set both the availability Matrix and the Vector version
            ossHeader->availabilityMatrix[i] = ossResourceDescriptors[i].countTotalResources = getRandomValue(2, 10);
        }
        else
        {
            // Set both the allocation Matrix and the Vector version
            ossHeader->availabilityMatrix[i] = ossResourceDescriptors[i].countTotalResources = 1;
        }
    }

    // For debugging
//    Print1DArray(ossHeader->availabilityMatrix, RESOURCES_MAX, RESOURCES_MAX);
//    isShutdown=true;

    try {   // Error trap

    // Start of main loop that will do the following
    // - Handle oss shutdown
    // - Create new processes in between 500-1000 msec intervals
    // - Handle child shutdowns
    // - Process requests for resources and distributing
    // - Handle deadlocks by selecting a victim and killing
    // - Gather statistics of each process
    // - assorted other misc items
    while(!isShutdown)
    {
        // Every loop gets 100-10000ns for processing time
        s.Wait();
        ossHeader->simClockNanoseconds += getRandomValue(10, 10000);
        if(ossHeader->simClockNanoseconds > 1000000000)
        {
            ossHeader->simClockSeconds += floor(ossHeader->simClockNanoseconds/1000000000);
            ossHeader->simClockNanoseconds -= 1000000000;
        }
        s.Signal();
        // ********************************************
        // Create New Processes
        // ********************************************
        // Check bitmap for room to make new processes
        if(nProcessCount < MAX_PROCESSES && !isKilled &&
            time(NULL) - secondsStart < 3)
        {
            // Check if there is room for new processes
            // in the bitmap structure
            int nIndex = 0;
            for(;nIndex < PROCESSES_MAX; nIndex++)
            {
                if(!bm.getBitmapBits(nIndex))
                {
                    cout << endl << "####### New Process #######" << endl;

                    // Found one.  Create new process
                    int newPID = forkProcess(ChildProcess, strLogFile, nIndex);

                    // Setup Shared Memory for processing
                    ossUserProcesses[nIndex].pid = newPID;

                    // Set bit in bitmap
                    bm.setBitmapBits(nIndex, true);

                    // Increment how many have been made
                    nProcessCount++;

                    // Increment out next target to make a new process
                    nNextTargetStartTime+=getRandomValue(1, 500);
                    cout << "Next Target Time: " << nNextTargetStartTime << endl;

                    // Log it
                    LogItem("OSS  ", ossHeader->simClockSeconds,
                        ossHeader->simClockNanoseconds, "Generating new process", 
                        newPID,
                        nIndex, strLogFile);

                    // Log Process Status
                    LogItem(bm.getBitView(), strLogFile);

                    // Every new process gets 1-500ms for scheduling time
                    s.Wait();
                    ossHeader->simClockNanoseconds += getRandomValue(1000, 500000);
                    s.Signal();
                }
            }
        }

        // ********************************************
        // Handle Ctrl-C or End Of Simulation
        // ********************************************
        // Terminate the process if CTRL-C is typed
        // or if the max time-to-process has been exceeded
        // but only send out messages to kill once
        if((sigIntFlag || (time(NULL)-secondsStart) > maxTimeToRunInSeconds) && !isKilled)
        {
            isKilled = true;
            // Send signal for every child process to terminate
            for(int nIndex=0;nIndex<PROCESSES_MAX;nIndex++)
            {
                // Send signal to close if they are in-process
                if(bm.getBitmapBits(nIndex))
                {
                    // Kill it and update our bitmap
                    kill(ossUserProcesses[nIndex].pid, SIGQUIT);
                    bm.setBitmapBits(nIndex, false);
                }
            }

            // We have notified children to terminate immediately
            // then let program shutdown naturally -- that way
            // shared resources are deallocated correctly
            if(sigIntFlag)
            {
                errno = EINTR;
                perror("Killing processes due to ctrl-c signal");
            }
            else
            {
                errno = ETIMEDOUT;
                perror("Killing processes due to timeout");
            }
        }



        // ********************************************
        // Handle Child Shutdowns
        // ********************************************
        // Check for a PID
        // Note :: We use the WNOHANG to call waitpid without blocking
        // If it returns 0, it does not have a PID waiting
        int waitPID = waitpid(-1, &wstatus, WNOHANG | WUNTRACED | WCONTINUED);

        // No PIDs are in-process
        if (waitPID == -1)
        {
            isShutdown = true;
            continue;
        }

        // A PID Exited
        if (WIFEXITED(wstatus) && waitPID > 0)
        {
//            cout << "O: ********************* Exited: " << waitPID << endl;

            // Find the PID and remove it from the bitmap
            for(int nIndex=0;nIndex<PROCESSES_MAX;nIndex++)
            {
                if(ossUserProcesses[nIndex].pid == waitPID)
                {

                    // Reset to start over
                    ossUserProcesses[nIndex].pid = 0;
                    bm.setBitmapBits(nIndex, false);

                    LogItem("OSS  ", ossHeader->simClockSeconds,
                        ossHeader->simClockNanoseconds, "Process signaled shutdown", 
                        waitPID,
                        nIndex, strLogFile);

                    break;
                }
            }

        } else if (WIFSIGNALED(wstatus) && waitPID > 0) {
            cout << waitPID << " killed by signal " << WTERMSIG(wstatus) << endl;
        } else if (WIFSTOPPED(wstatus) && waitPID > 0) {
            cout << waitPID << " stopped by signal " << WTERMSIG(wstatus) << endl;
        } else if (WIFCONTINUED(wstatus) && waitPID > 0) {
        }

        // ********************************************
        // Manage Resource Requests
        // ********************************************
        if(!isKilled)
        {
            // First fullfil any waiting requests in request queue
            // Receive a message if any available
            if(msgrcv(msgid, (void *) &msg, sizeof(message), OSS_MQ_TYPE, IPC_NOWAIT) > 0)
            {
                cout << "OSS ####### Got Message: " << msg.action << " : " << msg.procPid << " : " << msg.resIndex << endl;

                if(msg.action==REQUEST_SHUTDOWN)
                {
                    // Go through each resource
                    for(int i=0; i < RESOURCES_MAX; i++)
                    {
            cout << "OSS ####### In Shutdown " << i << endl;
                        // Find any resources held by this message and clear them out
                        for(vector<int>::iterator resItem = 
                            ossResourceDescriptors[i].allocatedProcs.begin(); 
                            resItem != ossResourceDescriptors[i].allocatedProcs.end(); ++resItem)
                        {
                            if(*resItem == msg.procPid)
                            {
                                ossResourceDescriptors[i].allocatedProcs.erase(resItem);
                                ossResourceDescriptors[i].countReleased++;
                            }
                        }
                    }
                    // Send back the message to continue shutdown
                    msg.action = OK;
                    msg.type = msg.procPid;
                    int n = msgsnd(msgid, (void *) &msg, sizeof(message), IPC_NOWAIT);
                }
                else if(msg.action==REQUEST_CREATE)
                {
            cout << "OSS ####### In Resource Create " << msg.procPid << " : " << msg.resIndex << endl;
                    ossResourceDescriptors[msg.resIndex].countRequested++;

                    // Check if this resource is available, if so allocate
                    if(ossResourceDescriptors[msg.resIndex].countTotalResources > 
                        ossResourceDescriptors[msg.resIndex].allocatedProcs.size())
                    {
                        ossResourceDescriptors[msg.resIndex].allocatedProcs.push_back(msg.procPid);
                        ossResourceDescriptors[msg.resIndex].countAllocated++;    
                    
                        // Update our matrix
                        int nNewVal = Get1DArrayValue(ossHeader->allocatedMatrix, msg.procIndex, msg.resIndex, RESOURCES_MAX);
                        Set1DArrayValue(ossHeader->allocatedMatrix, msg.procIndex, msg.resIndex, RESOURCES_MAX, nNewVal+1);
//                Print1DArray(ossHeader->allocatedMatrix, RESOURCES_MAX*PROCESSES_MAX, RESOURCES_MAX);

                        // Send success message back
                        msg.action = OK;
                        msg.type = msg.procPid;
                        int n = msgsnd(msgid, (void *) &msg, sizeof(message), IPC_NOWAIT);
                    }
                    else //  put in wait queue
                    {
                        ossResourceDescriptors[msg.resIndex].waitingQueue.push_back(msg.procPid);
                        ossResourceDescriptors[msg.resIndex].countWaited++;
                        // Update the requestMatrix
                        int nNewVal = Get1DArrayValue(ossHeader->requestMatrix, msg.procIndex, msg.resIndex, RESOURCES_MAX);
                        Set1DArrayValue(ossHeader->requestMatrix, msg.procIndex, msg.resIndex, RESOURCES_MAX, nNewVal+1);
            cout << "OSS ####### WAIT for Resource " << msg.procPid << " : " << msg.resIndex << " - " << ossResourceDescriptors[msg.resIndex].waitingQueue.size() << endl;
                    }
                }
                else if(msg.action==REQUEST_DESTROY)
                {
                    cout << "OSS ####### In Resource Destroy " << msg.procPid << " : " << msg.resIndex << endl;
                    for(vector<int>::iterator resItem = 
                        ossResourceDescriptors[msg.resIndex].allocatedProcs.begin(); 
                        resItem != ossResourceDescriptors[msg.resIndex].allocatedProcs.end(); ++resItem)
                    {
                        if(*resItem == msg.procPid)
                        {
                            ossResourceDescriptors[msg.resIndex].allocatedProcs.erase(resItem);
                            ossResourceDescriptors[msg.resIndex].countReleased++;

//                Print1DArray(ossHeader->allocatedMatrix, RESOURCES_MAX*PROCESSES_MAX, RESOURCES_MAX);

                            // Decrement the matrix
                            int nNewVal = Get1DArrayValue(ossHeader->allocatedMatrix, msg.procIndex, msg.resIndex, RESOURCES_MAX);
                            Set1DArrayValue(ossHeader->allocatedMatrix, msg.procIndex, msg.resIndex, RESOURCES_MAX, max(nNewVal-1, 0));

                            break;
                        }
                    }
                    // Send success message back
                    msg.action = OK;
                    msg.type = msg.procPid;
                    int n = msgsnd(msgid, (void *) &msg, sizeof(message), IPC_NOWAIT);
                }
            }
        }
        
        // ********************************************
        // Move Waiting Resource Requests into Freed Areas if avail
        // ********************************************

        // Loop through each resource to see if there is any room
        // available and that there is a waiting resource
        for(int i=0; i < RESOURCES_MAX; i++)
        {
            if(ossResourceDescriptors[i].countTotalResources >
                ossResourceDescriptors[i].allocatedProcs.size() &&
                ossResourceDescriptors[i].waitingQueue.size() > 0)
            {
//cout << "Got here 4 : " << ossResourceDescriptors[i].waitingQueue.size() << endl;
                // Just take the top one off and insert it (for now)
                int nWaitingProc = ossResourceDescriptors[i].waitingQueue.front();
//cout << "Got here 4.1" << endl;

                assert(!ossResourceDescriptors[i].waitingQueue.empty());
                ossResourceDescriptors[i].waitingQueue.erase(ossResourceDescriptors[i].waitingQueue.begin());

            cout << "OSS ####### In Wait Resource Alloc " << nWaitingProc << " : " << i << endl;
                ossResourceDescriptors[i].allocatedProcs.push_back(nWaitingProc);
                ossResourceDescriptors[i].countAllocated++;

                // Find the Proc Index from the PID
                int nIndex = -1;
                for(int j=0; j < PROCESSES_MAX; j++)
                {
                    if(ossUserProcesses[j].pid == nWaitingProc)
                        nIndex = j;
                }

                if(nIndex > -1)
                {
                // Update our Matrices - Allocated & Request

//                Print1DArray(ossHeader->requestMatrix, RESOURCES_MAX*PROCESSES_MAX,RESOURCES_MAX);
                int nNewVal = Get1DArrayValue(ossHeader->allocatedMatrix, nIndex, i, RESOURCES_MAX);
                Set1DArrayValue(ossHeader->allocatedMatrix, nIndex, i, RESOURCES_MAX, nNewVal+1);
                nNewVal = Get1DArrayValue(ossHeader->requestMatrix, nIndex, i, RESOURCES_MAX);
                Set1DArrayValue(ossHeader->requestMatrix, nIndex, i, RESOURCES_MAX, nNewVal-1);
                }

                // Send success message back
                msg.action = OK;
                msg.type = nWaitingProc;
                int n = msgsnd(msgid, (void *) &msg, sizeof(message), IPC_NOWAIT);
            }
        }

        // ********************************************
        // Check for Deadlocks
        // ********************************************
        // Using the deadlock detection algorithms to find the deadlock
        // Only once per second
        if((time(NULL)-secondsStart) > deadlockTimer)
        {
            deadlockTimer++;
            int nDeadlockProcess = deadlock(ossHeader->availabilityMatrix, 
            PROCESSES_MAX, RESOURCES_MAX, ossHeader->requestMatrix, ossHeader->allocatedMatrix);

            // Deadlock found, release a resource from this process
            if(nDeadlockProcess > -1)
            {
                cout << "******** DDDDEADLOCK ********: " << nDeadlockProcess << endl;
    //            Print1DArray(ossHeader->availabilityMatrix, RESOURCES_MAX, RESOURCES_MAX);
                cout << endl;
                Print1DArray(ossHeader->allocatedMatrix, RESOURCES_MAX*PROCESSES_MAX, RESOURCES_MAX);
    //            Print1DArray(ossHeader->requestMatrix, RESOURCES_MAX*PROCESSES_MAX, RESOURCES_MAX);

                // Kill it and update our bitmap
                kill(ossUserProcesses[nDeadlockProcess].pid, SIGQUIT);
                bm.setBitmapBits(nDeadlockProcess, false);
            }
        }
    } // End of main loop
    } catch( ... ) {
        cout << "An error occured.  Shutting down shared resources" << endl;
    }

    // Get the stats from the shared memory before we break it down
    nTotalTime = ossHeader->simClockSeconds;

    // Breakdown shared memory
    // Dedetach shared memory segment from process's address space

    LogItem("________________________________\n", strLogFile);
    LogItem("OSS: De-allocating shared memory", strLogFile);

    if (shmdt(shm_addr) == -1) {
        perror("OSS: Error detaching shared memory");
    }

    // De-allocate the shared memory segment.
    if (shmctl(shm_id, IPC_RMID, NULL) == -1) {
        perror("OSS: Error deallocating shared memory ");
    }
    LogItem("OSS: Shared memory De-allocated", strLogFile);

    // Destroy the Message Queue
    msgctl(msgid,IPC_RMID,NULL);

    LogItem("OSS: Message Queue De-allocated", strLogFile);

    if(nCPUProcessCount > 0 && nIOProcessCount > 0)
    {
        // Calc & Report the statistics
        LogItem("________________________________\n", strLogFile);
        LogItem("OSS Statistics", strLogFile);
        LogItem("\t\t\tI/O\t\tCPU", strLogFile);
//        LogItem("Total\t\t\t" + GetStringFromInt(nIOProcessCount) + "\t\t" + GetStringFromInt(nCPUProcessCount), strLogFile);
//        string strIOStat = GetStringFromFloat((float)nTotalTime/(float)nIOProcessCount);
//        string strCPUStat = GetStringFromFloat((float)nTotalTime/(float)nCPUProcessCount);
//        LogItem("Avg Sys Time\t\t" + strIOStat + "\t\t" + strCPUStat, strLogFile);
//        LogItem("Avg Wait Time:\t" + strIOStat + "ns", strLogFile);
//        strCPUStat = GetStringFromFloat((float)nCPU_IdleTime);
//        LogItem("CPU Idle Time:\t" + strCPUStat + "\n", strLogFile);
    }
    // Success!
    return EXIT_SUCCESS;
}


// ForkProcess - fork a process and return the PID
int forkProcess(string strProcess, string strLogFile, int nArrayItem)
{
        pid_t pid = fork();
        // No child made - exit with failure
        if(pid < 0)
        {
            // Signal to any child process to exit

            perror("OSS: Could not fork process");
            return EXIT_FAILURE;
        }
        // Child process here - Assign out it's work
        if(pid == 0)
        {
            // Execute child process without array arguements
            if(nArrayItem < 0)
              execl(strProcess.c_str(), strProcess.c_str(), strLogFile.c_str(), "25", (char*)0);
            else
            {
              // Convert int to a c_str to send to exec
              string strArrayItem = GetStringFromInt(nArrayItem);
              execl(strProcess.c_str(), strProcess.c_str(), strArrayItem.c_str(), strLogFile.c_str(), "25", (char*)0);
            }

            fflush(stdout); // Mostly for debugging -> tty wasn't flushing
            exit(EXIT_SUCCESS);    // Exit from forked process successfully
        }
        else
            return pid; // Returns the Parent PID
}

