# Resource Management (oss) Application

Resource Management (oss) application by Brett Huffman for CMP SCI 4760 - Project 5 - V1.0

In this project, 

A log was kept of each day's activity.  It is found at the end of this README file.

A Git repository was maintained with a public remote found here: https://github.com/dicer2000/ResourceManagement.git

## Assumptions
There were some items I didn't understand about the project's operation.  Based on the feedback I did receive, I made these assumptions:

1. All times are processed and shown in Seconds:Nanoseconds
2. Added -v as a verbose mode per pg 2 of instructions

## Program Switches
The program can be invoked as:

```
oss [-h] 
oss [-v]
  -h Describe how the project should be run, then terminate.
  -v puts the logfile output into Verbose Mode
```

## Install
To install this program, clone it with git to the folder to which you want 
it saved.
```
git clone https://github.com/dicer2000/ResourceManagement.git
```
## Compile
To compile the master application, simply run the make command:
```
make
```
## Run
To run the program, use the oss command.  You can use any of the command line options listed in program switches area.

## Problems / Issues

The biggest problems experienced in this project was around Segmentation Faults I was getting when working with all the vectors and arrays.  I develop on a Mac, then move my code to the Hoare.  I find my environment a lot more helpful to develop in that way.

However, one big problem with doing that is that there are environmental differences.  This problem seems to be around those differences.  The Mac implementation of erasing vector entries must do some bounds checking.  While the Linux version does not.  When I moved my code over to Hoare, it continually failed due to Segmentation errors.

What I found that indeed, there were problems with me properly checking bounds.  I had to do lots of trial and error to find it, however.

Thank goodness for Assert!

## Work Log

- 4/5/2021 - Setup initial project files and make file
- 4/6/2021 - Continued setup of project
- 4/7/2021 - Continued setup; Added deadlock code from lectures
- 4/8/2021 - Setup clock to work with this project; Resource Descriptors
- 4/9/2021 - Created the control semaphore; Debugging; Created main loop of oss; created main loop of user_proc; Debugging
- 4/10/2021 - Re-writing the request mechanism; Resource Request vector; Debugging
- 4/11/2021 - Testing; Putting requests in shared memory
- 4/12/2021 - Continued testing
- 4/14/2021 - Testing; Keeping track of processes that are closing; Setting up Deadlock testing
- 4/15/2021 - Fixing errors with Segmentation Faults; Continued timing and testing

*©2021 Brett W. Huffman*