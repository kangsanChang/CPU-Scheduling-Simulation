#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include "helper.h"

// DEBUG 용으로 만든 함수.
// void printProcess(Process p)
// {
// printf("========================\n");
// printf("[pid : %d] ", p.pid);
// printf("[arrivalTime : %d] ", p.arrivalTime);
// printf("[bursts current/total : %d / %d] ", p.currentBurst, p.numOfBursts);
// printf("[burst step/length : %d / %d] ", p.bursts[p.currentBurst].step, p.bursts[p.currentBurst].length);
// printf("[remainQtm : %d] \n", p.quantumRemaining);
// printf("========================\n\n");
// }

// 전역 변수
Process processes[MAX_PROCESSES + 1]; // process를 담는 배열
int numberOfProcesses;								// 전체 process 의 수
int nextProcess;											// 다음에 실행해야 할 process 의 index 를 가리키는 변수.
int totalWaitingTime;
int totalContextSwitches;
int cpuTimeUtilized;
int theClock; // 프로그램을 수행하면서 main loop 를 한 번 돌때마다 1씩증가하는 counter. process들이 실행되고, 종료되는 시간의 기준으로 사용.
int sumTurnarounds;
int timeQuantum; // 입력받은 timeQuantum 값

// readyQueue, waitingQueue 만듦
Process_queue readyQueue;
Process_queue waitingQueue;

// Processor 의 수 만큼 Process를 처리할 수 있는 CPU 만듦
// 각 CPU는 하나의 Process 를 가지고 있으면서 burst를 처리함.
Process *CPUS[NUMBER_OF_PROCESSORS];

// Temporary "Pre-Ready" queue
Process *tmpQueue[MAX_PROCESSES + 1];
int tmpQueueSize;

// Queue에 넣을 Process Node 를 생성 및 초기화 하는 함수.
Process_node *createProcessNode(Process *p)
{
	Process_node *node = (Process_node *)malloc(sizeof(Process_node));
	if (node == NULL)
	{
		error("out of memory");
	}
	node->data = p;
	node->next = NULL;
	return node;
}

// global variable 을 0으로 초기화하는 함수.
void resetVariables(void)
{
	numberOfProcesses = 0;
	nextProcess = 0;
	totalWaitingTime = 0;
	totalContextSwitches = 0;
	cpuTimeUtilized = 0;
	theClock = 0;
	sumTurnarounds = 0;
	tmpQueueSize = 0;
}

// Process 가 저장될 process_queue 생성 후 초기화.
void initializeProcessQueue(Process_queue *q)
{
	q = (Process_queue *)malloc(sizeof(Process_queue));
	q->front = q->back = NULL;
	q->size = 0;
}

// Queue 안에 새로운 process 넣는 함수. (enqueue)
void enqueueProcess(Process_queue *q, Process *p)
{
	Process_node *node = createProcessNode(p); // 새로 넣을 process 를 담고 있는 process_node 생성
	if (q->front == NULL)
	{
		assert(q->back == NULL);	 // process_queue의 front가 null인데, back이 null이 아니면 에러를 출력 (비어있는 상황이면 둘 다 null이 되어야 함.)
		q->front = q->back = node; // queue에 최초로 node 를 insert 하는 경우. front 와 back 이 가리키는 procesS_node 는 동일함.
	}
	else
	{
		assert(q->back != NULL); // front 가 null이 아닐 때 back 이 null 이면 에러를 출력 (1개라도 node 가 들어가 있으면, front, back이 다 존재해야함)
		q->back->next = node;		 // 현재 마지막에 있는 (q->back) process_node 의 next 로 insert 하는 process_node 의 주소를 줌.
		q->back = node;					 // back 을 가리키던 포인터를 다음 node 의 주소로 바꿈. ( Queue 는 FIFO 이므로, back 의 포인터를 옮겨 새 node 를 insert 함)
	}
	q->size++; // queue 에 저장된 process_node 의 수 (size) 증가.
}

// process_queue 의 맨 앞 process 삭제.
// CPU 에 Process 할당 시 readyQueue 에서 해당 Process 제거할 때 사용.
void dequeueProcess(Process_queue *q)
{
	Process_node *deleted = q->front; // queue 의 맨 앞 node 를 가져옴.
	assert(q->size > 0);							// queue 의 사이즈가 0 보다 크지 않으면 오류. (삭제 할 process 가 없으므로)
	if (q->size == 1)
	{ // queue 의 size 가 1 인 경우. 삭제하면 queue 가 비게 되므로, front 와 back 의 포인터를 NULL 로 만들어줌.
		q->front = NULL;
		q->back = NULL;
	}
	else
	{
		assert(q->front->next != NULL); // queue 에 저장된 node 가 2개 이상이므로, queue의 첫 부분에 있는 node 의 next 가 가리키는 node 가 있어야 함. 없으면 에러
		q->front = q->front->next;			// queue의 front 를 다음 node 로 옮김.
	}
	free(deleted); // 삭제할 node의 memory allocation 해제
	q->size--;		 // queue 의 사이즈 1 줄임.
}

// process가 Queue에서의 평균 대기 시간 계산해주는 함수.
double averageWaitTime(int theWait)
{
	double result = theWait / (double)numberOfProcesses;
	return result;
}

// 각 process들이 도착하여 실행이 끝나기 까지 걸린 총 시간(TurnaroundTime)의 평균
double averageTurnaroundTime(int theTurnaround)
{
	double result = theTurnaround / (double)numberOfProcesses;
	return result;
}

// 평균 CPU 사용률
double averageUtilizationTime(int theUtilization)
{
	double result = (theUtilization * 100.0) / theClock;
	return result;
}

// 전체 process 중 아직 도착하지 않은(incoming 해야 할) process 의 수
int totalIncomingProcesses(void)
{
	return numberOfProcesses - nextProcess;
}

// 두 process 간 도착 시간을 비교하는 함수 (오름차순)
int compareArrivalTime(const void *a, const void *b)
{
	Process *first = (Process *)a;
	Process *second = (Process *)b;
	return first->arrivalTime - second->arrivalTime;
}

// 두 process 간 process id 를 비교하는 함수 (오름차순)
int compareProcessIds(const void *a, const void *b)
{
	Process *first = (Process *)a;
	Process *second = (Process *)b;
	if (first->pid == second->pid)
	{
		error_duplicate_pid(first->pid); // pid 가 동일할 경우 error 처리
	}
	return first->pid - second->pid;
}

int runningProcesses(void)
{
	int runningProcesses = 0;
	int i;
	for (i = 0; i < NUMBER_OF_PROCESSORS; i++)
	{
		if (CPUS[i] != NULL)
		{
			runningProcesses++;
		}
	}
	return runningProcesses;
}

Process *nextScheduledProcess(void)
{
	if (readyQueue.size == 0)
	{
		return NULL; // readyQueue 의 크기가 0 인 경우 (현재 처리해야할 process 가 없을 때) : null 반환
	}
	Process *grabNext = readyQueue.front->data; // ready queue의 가장 앞에 있는 process(process_node의 data)의 주소를 가져옴
	dequeueProcess(&readyQueue);								// readyQueue 에서 CPU가 수행하도록 넘길 process(queue의 첫번째 node)를 deque 함. (readyQueue에서 빼냄)
	return grabNext;														// CPU에 수행하도록 넘길 Process 의 주소값 반환.
}
// 새롭게 실행해야 할 process 를 ready 전 상태의 '임시 queue' 에 추가하는 함수.
// nextProcess: processes 배열에서 process 들을 index에 순차적으로 실행하기 위한 iterator
// numberOfProcesses: processes 에 담긴 proccess 의 총 갯수
void addNewIncomingProcess(void)
{
	while (nextProcess < numberOfProcesses && processes[nextProcess].arrivalTime <= theClock)
	{
		// 다음 process(새 process)를 추가하기 위해서는 다음 process 가 전체 process의 갯수를 넘지 않으면서 (현재 process가 마지막이 아니면서)
		// 새 process가 실행되는것을 simulate 하므로, 다음 process 가 도착한 시간이 되었을 때 수행해야 한다.
		// 그러므로 clock (이 프로그램이 수행을 시작하여 돌고있는 현재 실행시간) 과 추가할 process 의 도착시간(arrivalTime) 이 같거나 더 커야한다.

		tmpQueue[tmpQueueSize] = &processes[nextProcess];				// 임시 queue(process* 들을 저장하는 배열)에 추가할 process의 주소를 줌.
		tmpQueue[tmpQueueSize]->quantumRemaining = timeQuantum; // main 함수 실행 시 입력받은 실행시간 (timeQuantum)을 해당 process 의 남은시간(quantumRemaining)으로 넣어줌 (수행시간 initialize)
		tmpQueueSize++;																					// 임시 queue 에 process 를 하나 넣었으므로 갯수 증가.
		nextProcess++;																					// next process 하나 증가.
	}
}

// waitingQueue 의 첫번째 node 의 burst 상황을 검사하여, 끝났으면 다음 burst로 넘어갈 수 있도록
// currentBurst 를 하나 늘려주고, timeQuantum 을 채운 후 임시 queue 로 프로세스를 보낸다.
void waitingToReady(void)
{
	int i;																		// iterator
	int waitingQueueSize = waitingQueue.size; // waitingQueue 의 size 를 저장.
	for (i = 0; i < waitingQueueSize; i++)
	{																								// waitingQueue의 모든 process를 looping
		Process *grabNext = waitingQueue.front->data; // waitingQueue 의 첫 번째 process(front->data)의 주소를 저장
		dequeueProcess(&waitingQueue);								// waitingQueue 에서 저장한 process 제거
		if (grabNext->bursts[grabNext->currentBurst].step == grabNext->bursts[grabNext->currentBurst].length)
		{
			// Process 에서 처리하던 bursrt 는 다 끝냈는데 아직 처리할 burst 가 더 남아있는 경우
			// 다음 burst 로 넘어갈 수 있게 함.
			grabNext->currentBurst++;									// 다음 burst 를 수행할 수 있게 currentBurst 하나 증가
			grabNext->quantumRemaining = timeQuantum; // timeQueantum 초기화. (수행하던 process 는 끝났으므로 새로 CPU를 할당받았을 때 (초기화된상태에서) 실행됨))
			grabNext->endTime = theClock;							// 현재 시간으로 process 가 끝난 시간 기록.
			tmpQueue[tmpQueueSize++] = grabNext;			// 임시 Queue 에 해당 Process insert.
		}
		else
		{
			// Process 가 실행중이던 burst 가 다 끝나지 않은 경우, waitingQueue 의 제일 뒤쪽으로 다시 보낸다.
			enqueueProcess(&waitingQueue, grabNext);
		}
	}
}

// 임시 queue 에 있는 process 들을 readyQueue 로 옮기고, 비어있는 CPU Core를 할당하는 함수.
void readyToRunning(void)
{
	int i;
	qsort(tmpQueue, tmpQueueSize, sizeof(Process *), compareProcessIds); // 임시 queue 에 있는 process들을 pid 순으로 정렬
	for (i = 0; i < tmpQueueSize; i++)
	{
		enqueueProcess(&readyQueue, tmpQueue[i]); // readyQueue에 임시 queue에 있는 process들을 넣음.
	}
	tmpQueueSize = 0; // readyQueue 에 임시 queue의 모든 process넣은 후 임시 queue의 size를 0으로 리셋함.
	for (i = 0; i < NUMBER_OF_PROCESSORS; i++)
	{
		if (CPUS[i] == NULL)
		{																		// process 를 처리하고 있지 않은(idle) CPU 코어가 있으면
			CPUS[i] = nextScheduledProcess(); // readyQueue의 첫 번째 process 를 넣어줌 (nextScheduledProcess 함수 이용)
		}
	}
}

void runningToWaiting(void)
{
	int num = 0;															 // context switching 이 일어나서 process 의 상태를 저장해야 할 경우 저장할 preemptive 배열에서 사용할 index (즉 burst 수행중 시간이 만료되어 context switching 이 일어나는 process들의 수)
	Process *preemptive[NUMBER_OF_PROCESSORS]; // context switching 이 일어난 Process들의 주소값 배열
	int i, j;																	 // iterators

	// CPU는 현재 자신이 수행중인 Process를 가지고 있음.
	// loop 을 돌면서 모든 CPU에서 돌아가는 Process를 따짐
	for (i = 0; i < NUMBER_OF_PROCESSORS; i++)
	{
		if (CPUS[i] != NULL)
		{ // CPU 가 비어있지 않은 경우 (Process 를 수행중이던 상태)

			// CPU에서 수행중인 Process의 한 burst 가 끝났으면 (step == length), 남은 시간에 관계 없이 사용했던 CPU를 반납함.
			if (CPUS[i]->bursts[CPUS[i]->currentBurst].step == CPUS[i]->bursts[CPUS[i]->currentBurst].length)
			{
				CPUS[i]->currentBurst++; // 다음 burst 수행할 수 있도록 process의 currentBurst 1 증가.
				if (CPUS[i]->currentBurst < CPUS[i]->numOfBursts)
				{																					// currentBurst 가 수행할 마지막 burst 가 아니면 (Process가 끝이 아니면)
					enqueueProcess(&waitingQueue, CPUS[i]); // 수행중인 Process를 watingQueue에 넣어줌. (IO burst 수행하러 감)
				}
				else
				{															 // 마지막 burst 까지 수행을 완료 한 경우. (currentBurst == numOfBursts) (Process 가 끝난 경우)
					CPUS[i]->endTime = theClock; // CPU 에 있는 process의 endTime에 현재까지 경과 시간 (프로그램 시작 후 부터 돌아가는 clock) 을 넣어줌
																			 // Process 종료. (마지막 CPU burst 수행 다 했음. 항상 끝은 cpu burst 로 나야함.)
				}
				CPUS[i] = NULL; // 해당 CPU 를 비워준다.
			}

			// CPU가 수행중인 Process의 burst를 처리중이던 경우. (step < length)
			else if (CPUS[i]->quantumRemaining == 0)
			{																									 // 할당받은 시간을 모두 사용 한 경우 => context switching
				preemptive[num] = CPUS[i];											 // CPU가 수행중인 Process의 상태 저장
				preemptive[num]->quantumRemaining = timeQuantum; // preemptive 에 저장된 process의 수행 가능 시간(quantumRemaining)을 다시 채워줌.
				num++;																					 // 다음 CPU에서도 context switching 이 일어날 수 있으므로, 저장할 수 있게 index 하나 증가.
				totalContextSwitches++;													 // context switching 이 일어난 횟수 +1 기록. (프로그램 종료 시 수행 결과 출력 용도)
				CPUS[i] = NULL;																	 // 해당 CPU 를 비워줌.
			}

			// ****
			// 수행 시간(quantumRemaining) 이 남아있는 경우 CPU를 비우지 않음
			// ****
		}
	}
	// context switching 이 일어난 process 들을 process id 순으로 오름차순 정렬
	qsort(preemptive, num, sizeof(Process *), compareProcessIds);
	// context switching 이 일어난 process 들을 ready queue에 넣어줌.(없으면(num이 0일때) 수행하지 않음)
	for (j = 0; j < num; j++)
	{
		enqueueProcess(&readyQueue, preemptive[j]);
	}
}
/**
 * Function to update waiting processes, ready processes, and running processes
 */
// waiting, ready, running 중인 process 들의 상태를 update 하는 함수
void updateStates(void)
{
	int i;																		// iterator
	int waitingQueueSize = waitingQueue.size; // waitingQueue size 대입
	// waitingQueue의 모든 process 의 상태를 업데이트 (burst 의 step 증가)
	for (i = 0; i < waitingQueueSize; i++)
	{
		Process *grabNext = waitingQueue.front->data; // waitingQueue의 첫 번째 process 를 가져옴.
		dequeueProcess(&waitingQueue);
		grabNext->bursts[grabNext->currentBurst].step++; // currentBurst 의 burst 수행 상태(step) 1증가.
		enqueueProcess(&waitingQueue, grabNext);
	}
	// readyQueue의 상태 업데이트 (각 process의 waiting Time 을 1 씩 증가시킴)
	for (i = 0; i < readyQueue.size; i++)
	{
		Process *grabNext = readyQueue.front->data;
		dequeueProcess(&readyQueue);
		grabNext->waitingTime++;
		enqueueProcess(&readyQueue, grabNext);
	}

	// CPU 내부에 있는 process 들의 step 을 1씩 증가시킴.
	for (i = 0; i < NUMBER_OF_PROCESSORS; i++)
	{
		if (CPUS[i] != NULL)
		{
			CPUS[i]->bursts[CPUS[i]->currentBurst].step++;
			CPUS[i]->quantumRemaining--;
		}
	}
}
/**
 * Display results for average waiting time, average turnaround time, the time
 * the CPU finished all processes, average CPU utilization, number of context
 * switches, and the process ID of the last process to finish.
 */
// 결과를 display 하는 함수.
void displayResults(float awt, float atat, int sim, float aut, int cs, int pids)
{
	printf("------------------Round-Robin------------------\n"
				 "Average waiting time\t\t:%.2f units\n"
				 "Average turnaround time\t\t:%.2f units\n"
				 "Time CPU finished all processes\t:%d\n"
				 "Average CPU utilization\t\t:%.1f%%\n"
				 "Number of context Switces\t:%d\n"
				 "PID of last process to finish\t:%d\n"
				 "------------------------------------------------\n",
				 awt, atat, sim, aut, cs, pids);
}

// input 시 timeQuantum 값 입력받음.
int main(int argc, char *argv[])
{
	// 필요한 변수 선언 및 초기화
	int i;											 // iterator
	int status = 0;							 // readProcess 의 상태 flag
	double ut, wt, tat;					 // ut : CPU 이용 시간(utilization time), wt : 대기시간 (waiting time), tat : 반환시간 (turnaround time)
	int lastPID;								 // workload 에서 마지막으로 실행한 process 의 id
	timeQuantum = atoi(argv[1]); // 입력받은 timeQuantum 값을 정수로 변환

	// input error handling
	// 입력받은 timeQuantum 값이 잘못된 경우 error handling
	if (argc > 2)
	{
		printf("Incorrect number of arguments, only add one time slice.\n");
		exit(-1);
	}
	else if (argc < 2)
	{
		printf("Must add time slice.\n");
		exit(-1);
	}

	// clear CPU'S, initialize queues, and reset global variables
	// CPU(모든 Processor), ready Queue, waiting Queue, GlobalVariable 초기화
	for (i = 0; i < NUMBER_OF_PROCESSORS; i++)
	{
		CPUS[i] = NULL;
	}
	resetVariables(); // 전역변수들 초기화.
	initializeProcessQueue(&readyQueue);
	initializeProcessQueue(&waitingQueue);

	// read in workload and store processes

	// 넣어 준 data 에서 Process 를 읽어서 processes 배열에 넣어줌.
	// processes 배열의 해당 index 의 주소값을 주어 input_output.c 파일의 readProcess 함수가 읽은 data 를 parsing 하여 넣도록 함.
	// readProcess 함수는 정상적으로 processes 배열에 읽어온 process를 정상적으로 추가했을 시 1 을 반환함.
	// status 가 1 일때 while loop 돌아감.
	while ((status = (readProcess(&processes[numberOfProcesses]))))
	{
		if (status == 1)
		{
			numberOfProcesses++; // 정상적으로 process 를 하나 읽은 경우 Process 의 갯수 (numberOfProcesses) 를 하나 증가 시킴.
		}
		if (numberOfProcesses > MAX_PROCESSES || numberOfProcesses == 0)
		{
			// processes 배열의 최대 크기보다 process 의 수 (numberOfProcesses) 가 더 큰 경우나, 읽은 process의 수가 하나도 없는 경우
			// 에러 처리
			error_invalid_number_of_processes(numberOfProcesses);
		}
	}

	// 도착한 시간 (ArrivalTime) 순서로 data 에 있는 process 들을 정렬함.
	// ** Contribute 한 부분 **
	// 세 번째 parameter 가 sizeof(Process*)로 잘못 되어 있어서 qsort 가 수행되지 않아 수정함.
	qsort(processes, numberOfProcesses, sizeof(Process), compareArrivalTime);

	// sort 된 processes 의 log 출력
	// int j;
	// for(j=0 ; j< numberOfProcesses ; j++) { printProcess(processes[j]); }

	// processes 배열은 .dat 파일로부터 data를 읽어 process 들을 parsing 한 결과들을 ArrivalTime 기준으로 sorting 하여 가지고 있음.
	// 즉 시간(arrivalTime)순서로 process 들이 발생하는 것 처럼 simulation 하도록 담아놓은 전체 process를 의미함.

	// 메인 실행 loop
	// 전역변수 theClock이 1 증가하면 한 번 looping 하는 term 을 가짐.
	while (numberOfProcesses)
	{
		addNewIncomingProcess();
		// 새로운 process가 도착하는것을 simualtion 하는 함수. process의 arrivalTime 과 theClock 이 동일할때 수행된다.
		// 읽어온 Process 들을 임시 queue(tmpQueue) 에 넣음.

		runningToWaiting();
		// 수행중인 Process의 burst 수행 상황에 따라
		// 수행할 burst 남은 경우: wating queue로 이동
		// 모든 burst 수행 한 경우: 기록 후 종료
		// burst 수행 중 quantum 이 끝난경우: context switching 을 수행
		// quantum 이 남아있는 경우: 그대로 둠

		readyToRunning();
		// 임시 queue 에 있는 process 들을 readyQueue 로 옮기고, process 가 수행되도록 비어있는 CPU Core를 할당하는 함수.

		waitingToReady();

		updateStates();

		// break when there are no more running or incoming processes, and the waiting queue is empty
		if (runningProcesses() == 0 && totalIncomingProcesses() == 0 && waitingQueue.size == 0)
		{
			break;
		}

		cpuTimeUtilized += runningProcesses();
		theClock++;
	}

	// calculations
	for (i = 0; i < numberOfProcesses; i++)
	{
		sumTurnarounds += processes[i].endTime - processes[i].arrivalTime;
		totalWaitingTime += processes[i].waitingTime;

		if (processes[i].endTime == theClock)
		{
			lastPID = processes[i].pid;
		}
	}

	wt = averageWaitTime(totalWaitingTime);
	tat = averageTurnaroundTime(sumTurnarounds);
	ut = averageUtilizationTime(cpuTimeUtilized);

	displayResults(wt, tat, theClock, ut, totalContextSwitches, lastPID);

	return 0;
}