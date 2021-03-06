#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <sys/time.h>

#define ID_BASE 101
#define STUDENT_COUNT 75
#define GS 0
#define RS 1
#define EE 2
#define NUMBER_OF_SECTION 3
#define SECTION_CAPACITY 20

//student structure
struct studentStruct{
	int id;
	char* priority;
	int section;
	time_t arrivalTime;
	time_t leaveTime;
};

struct studentStruct GsQueue[STUDENT_COUNT];
struct studentStruct RsQueue[STUDENT_COUNT];
struct studentStruct EeQueue[STUDENT_COUNT];
struct studentStruct section1[SECTION_CAPACITY];
struct studentStruct section2[SECTION_CAPACITY];
struct studentStruct section3[SECTION_CAPACITY];

char* priority[] = {"GS","RS","EE"};

pthread_mutex_t GsQueueMutex;  // mutex protects Gs queue.
pthread_mutex_t RsQueueMutex;  // mutex protects Rs queue.
pthread_mutex_t EeQueueMutex;  // mutex protects EE queue.
pthread_mutex_t sectionsMutex[3]; //mutex protects sections.
pthread_mutex_t printMutex;  // mutex protects printing
pthread_mutex_t impatientMutex; // mutex protects impatient student queue.
pthread_mutex_t dropListMutex; // mutex protect drop student queue.

sem_t GsQueueSem;
sem_t RsQueueSem;
sem_t EeQueueSem;

struct itimerval profTimer;  // professor's office hour timer
time_t startTime;

//keep track of number of students in each sections.
int sectionCounts[3] = {0,0,0};

//keep track of number of students each queue processed.
int queueProcessed[3] = {0,0,0};

//keep track of student
struct studentStruct droppedList[STUDENT_COUNT];
int dropStudentCount = 0;

// keep track of impatient student left
struct studentStruct impatientList[STUDENT_COUNT];
int impatientStudentCount = 0;

//keep each queue's turn around time.
double queueTurnAround[3] = {0,0,0};

//keep track of number of student in each queue.
int GsQueuePOS = 0;
int RsQueuePOS = 0;
int EeQueuePOS = 0;

//total number of students has been processed.
int studentProcessed = 0;

//queue close signal
int queueOpen = 1;

int firstPrint = 1;

//function declarations.
int allSectionFull();
int completed();
void closeAllQueue();
void handleImpatientStudent(struct studentStruct* student,int time, int queueNum);

void print(char *event)
{
	time_t now;
	time(&now);
	double elapsed = difftime(now, startTime);
	int min = 0;
	int sec = (int) elapsed;

	if (sec >= 60) {
		min++;
		sec -= 60;
	}

	// Acquire the mutex lock to protect the printing.
	pthread_mutex_lock(&printMutex);

	if (firstPrint) {
		printf("TIME |  EVENT\n");
		firstPrint = 0;
	}

	// Elapsed time.
	printf("%1d:%02d | ", min, sec);

	printf("%s\n", event);

	// Release the mutex lock.
	pthread_mutex_unlock(&printMutex);
}

// A student arrives.
void studentArrives(struct studentStruct* student)
{
	int id = (int)student->id;
	char* priority = (char*) student->priority;
	char event[100];

	if(queueOpen != 0){

		if(strcmp(priority,"GS") == 0){
			pthread_mutex_lock(&GsQueueMutex);
			GsQueue[GsQueuePOS++] = *student;
			pthread_mutex_unlock(&GsQueueMutex);
			sprintf(event, "Student #%d.%s arrives Gs Queue and waits", id,priority);
			print(event);
			sem_post(&GsQueueSem);
		}
		else{
			if(strcmp(priority,"RS") == 0){
				pthread_mutex_lock(&RsQueueMutex);
				RsQueue[RsQueuePOS++] = *student;
				pthread_mutex_unlock(&RsQueueMutex);
				sprintf(event, "Student #%d.%s arrives Rs Queue and waits", id, priority);
				print(event);
				sem_post(&RsQueueSem);

			}
			else{
				pthread_mutex_lock(&EeQueueMutex);
				EeQueue[EeQueuePOS++] = *student;
				pthread_mutex_unlock(&EeQueueMutex);
				sprintf(event, "Student #%d.%s arrives Ee Queue and waits", id, priority);
				print(event);
				sem_post(&EeQueueSem);

			}
		}}
}



// The student thread.
void *student(struct studentStruct *student)
{
	// Students will arrive at random times during the office hour.
	sleep(rand()%120+0.1);

	time_t currentTime;
	time(&currentTime);
	student->arrivalTime = currentTime;
	studentArrives(student);
	return NULL;
}

//queue process
void queueProcess(int queueNum)
{
	int queue = queueNum;
	switch(queue){
	case 1:
		sem_wait(&GsQueueSem);
		if((allSectionFull() != 1) && (studentProcessed < STUDENT_COUNT) && (queueProcessed[0] < GsQueuePOS)){
			pthread_mutex_lock(&GsQueueMutex);
			struct studentStruct* temp = &GsQueue[queueProcessed[0]++];
			pthread_mutex_unlock(&GsQueueMutex);
			char event[80];
			time_t elapsed;
			time(&elapsed);
			if((elapsed - temp->arrivalTime) >= 10){
				handleImpatientStudent(temp,elapsed,GS);
			}
			else{
				sprintf(event, "Student #%d.%s is being process at Gs queue",  temp -> id, temp -> priority);
				print(event);

				int processTime = rand()%2 + 1;
				queueTurnAround[0] += processTime;

				sleep(processTime);

				time_t now;
				time(&now);
				temp->leaveTime = now;
				sprintf(event, "Student #%d.%s is finished at GS queue",  temp -> id, temp -> priority);
				print(event);

				int enroll;
				enroll = enrollStudent(temp);
				if(enroll != -1){
					sprintf(event, "Student #%d.%s is enrolled into Section %d",  temp -> id, temp -> priority, enroll +1 );
					printf("Section %d has %d students\n", enroll + 1, sectionCounts[enroll]);
					print(event);
				}
				else{
					sprintf(event, "Student #%d.%s is dropped",  temp -> id, temp -> priority);
					pthread_mutex_lock(&dropListMutex);
					droppedList[dropStudentCount++] = *(temp);
					pthread_mutex_unlock(&dropListMutex);
					print(event);
				}
				studentProcessed++;
				time_t leave;
				time(&leave);
				temp->leaveTime = leave;
			}
		}
		break;

	case 2:
		sem_wait(&RsQueueSem);
		if((allSectionFull() != 1) && (studentProcessed < STUDENT_COUNT) && (queueProcessed[1] < RsQueuePOS)){
			pthread_mutex_lock(&RsQueueMutex);
			struct studentStruct* temp = &RsQueue[queueProcessed[1]++];
			pthread_mutex_unlock(&RsQueueMutex);
			char event[80];
			time_t elapsed;
			time(&elapsed);
			if((elapsed - temp->arrivalTime) >= 10){
				handleImpatientStudent(temp,elapsed,RS);
			}
			else{
				sprintf(event, "Student #%d.%s is being process at RS queue",  temp -> id, temp -> priority);
				print(event);

				int processTime = rand()%3 + 2;
				queueTurnAround[1] += processTime;
				sleep(processTime);

				time_t now;
				time(&now);
				temp->leaveTime = now;
				sprintf(event, "Student #%d.%s is finished at RS queue",  temp -> id, temp -> priority);
				print(event);

				int enroll1;
				enroll1 = enrollStudent(temp);
				if(enroll1 != -1){
					sprintf(event, "Student #%d.%s is enrolled into Section %d",  temp -> id, temp -> priority, enroll1 +1);
					printf("Section %d has %d students\n", enroll1 + 1, sectionCounts[enroll1]);
					print(event);
				}
				else{
					sprintf(event, "Student #%d.%s is dropped",  temp -> id, temp -> priority);
					pthread_mutex_lock(&dropListMutex);
					droppedList[dropStudentCount++] = *(temp);
					pthread_mutex_unlock(&dropListMutex);
					print(event);
				}
				studentProcessed++;
				time_t leave;
				time(&leave);
				temp->leaveTime = leave;
			}
		}
		break;

	case 3:
		sem_wait(&EeQueueSem);
		if((allSectionFull() != 1) && (studentProcessed < STUDENT_COUNT) && (queueProcessed[2] < EeQueuePOS)){
			pthread_mutex_lock(&EeQueueMutex);
			struct studentStruct* temp = &EeQueue[queueProcessed[2]++];
			pthread_mutex_unlock(&EeQueueMutex);
			char event[80];
			time_t elapsed;
			time(&elapsed);
			if((elapsed - temp->arrivalTime) >= 10){
				handleImpatientStudent(temp,elapsed,EE);
			}

			else{
				sprintf(event, "Student #%d.%s is being process at EE queue",  temp -> id, temp -> priority);
				print(event);

				int processTime = rand()%4 + 3;
				queueTurnAround[2] += processTime;
				sleep(processTime);

				time_t now;
				time(&now);
				temp->leaveTime = now;
				sprintf(event, "Student #%d.%s is finished at EE queue",  temp->id, temp -> priority);
				print(event);

				int enroll2;
				enroll2 = enrollStudent(temp);
				if(enroll2 != -1){
					sprintf(event, "Student #%d.%s is enrolled into Section %d",  temp -> id, temp -> priority, enroll2 +1 );
					printf("Section %d has %d students\n", enroll2 + 1, sectionCounts[enroll2]);
					print(event);
				}
				else{
					sprintf(event, "Student #%d.%s is dropped",  temp -> id, temp -> priority);
					pthread_mutex_lock(&dropListMutex);
					droppedList[dropStudentCount++] = *(temp);
					pthread_mutex_unlock(&dropListMutex);
					print(event);
				}
				studentProcessed++;
				time_t leave;
				time(&leave);
				temp->leaveTime = leave;
			}
		}
		break;
	}}

// The queue thread.
void *queue(int param)
{
	int queue =  param;
	switch(queue){
	case 1:
		print("RS Queue is open");
		do{
			queueProcess(1);
		} while (completed() != 1);
		print("GS Queue is closed");
		closeAllQueue();
		return NULL;
		break;

	case 2:
		print("RS Queue is open");
		do{
			queueProcess(2);
		} while (completed() != 1);
		print("RS Queue is closed");
		closeAllQueue();
		return NULL;
		break;

	case 3:
		print("EE Queue is open");
		do{
			queueProcess(3);
		} while  (completed() != 1);
		print("EE Queue is closed");
		closeAllQueue();
		return NULL;
		break;
	}
}

//random Priority Generator
char* priorityGenerator(){
	int temp;
	temp = rand() % 3;
	switch(temp){
	case 0:
		return priority[0];
		break;
	case 1:
		return priority[1];
		break;
	case 2:
		return priority[2];
		break;
	}
	return "Non";
}

//random section generator
// section 3 meaning student can enroll any sections 1,2,3 with equal probability.
int sectionGenerator(){
	int temp;
	temp = (int) rand()%4;
	switch(temp){
	case 0:
		return 0;
		break;
	case 1:
		return 1;
		break;
	case 2:
		return 2;
		break;
	case 3:
		return 3;
		break;
	}
	return -1;
}

//check all 3 sections for availability.
int allSectionFull(){
	int i;
	for(i = 0; i < NUMBER_OF_SECTION; i++){
		if(sectionCounts[i] < SECTION_CAPACITY){
			return 0;
		}
	}
	return 1;
}

//handle close all queues, signal semaphore.
void closeAllQueue(){
	sem_post(&GsQueueSem);
	sem_post(&RsQueueSem);
	sem_post(&EeQueueSem);
	queueOpen = 0;
}

//complete if all students are processed or all sections are full.
int completed(){
	if((studentProcessed >= STUDENT_COUNT) || (allSectionFull() == 1)){
		return 1;
	}
	else{
		return 0;
	}
}

//enroll student into thier section.
int enrollStudent(struct studentStruct* student){
	int studentSec = student -> section;
	//studentsec == 3, meaning can enroll into any section.
	if(studentSec == 3){
		int i;
		for(i = 0; i < NUMBER_OF_SECTION; i++){
			if(sectionCounts[i] < SECTION_CAPACITY){
				studentSec = i;
			}
		}
	}

	//check each queue for availability and lock corresponding section queue.
	if(sectionCounts[studentSec] < SECTION_CAPACITY){
		switch(studentSec){
		case 0:
			pthread_mutex_lock(&sectionsMutex[studentSec]);
			section1[sectionCounts[studentSec]++] = *(student);
			pthread_mutex_unlock(&sectionsMutex[studentSec]);
			return studentSec;
			break;
		case 1:
			pthread_mutex_lock(&sectionsMutex[studentSec]);
			section2[sectionCounts[studentSec]++] = *(student);
			pthread_mutex_unlock(&sectionsMutex[studentSec]);
			return studentSec;
			break;
		case 2:
			pthread_mutex_lock(&sectionsMutex[studentSec]);
			section3[sectionCounts[studentSec]++] = *(student);
			pthread_mutex_unlock(&sectionsMutex[studentSec]);
			return studentSec;
			break;
		}
	}
	else{
		pthread_mutex_unlock(&sectionsMutex[studentSec]);
		return -1;
	}
}

//handle impatient student who leave after wait for 10 seconds
void handleImpatientStudent(struct studentStruct* student,int time, int queueNum){
	pthread_mutex_lock(&impatientMutex);
	char event[80];
	sprintf(event, "Student #%d.%s can't wait anymore and left %s queue",  student -> id, student -> priority, priority[queueNum]);
	print(event);
	student->leaveTime = time;
	impatientList[impatientStudentCount++] = *student;
	studentProcessed++;
	pthread_mutex_unlock(&impatientMutex);
}

// Main.
int main(int argc, char *argv[])
{
	int queueId = 1;

	// Initialize the mutexes and the semaphore.
	pthread_mutex_init(&printMutex, NULL);
	pthread_mutex_init(&GsQueueMutex, NULL);
	pthread_mutex_init(&RsQueueMutex, NULL);
	pthread_mutex_init(&EeQueueMutex, NULL);
	pthread_mutex_init(&sectionsMutex[0], NULL);
	pthread_mutex_init(&sectionsMutex[1], NULL);
	pthread_mutex_init(&sectionsMutex[2], NULL);
	pthread_mutex_init(&impatientMutex, NULL);
	pthread_mutex_init(&dropListMutex, NULL);

	sem_init(&GsQueueSem, 0, 0);
	sem_init(&RsQueueSem, 0, 0);
	sem_init(&EeQueueSem, 0, 0);

	srand(time(0));
	time(&startTime);

	// Create the queue threads.
	pthread_t queueThreadId;
	pthread_attr_t queueAtr;
	pthread_attr_init(&queueAtr);
	pthread_create(&queueThreadId, &queueAtr, queue, queueId);

	pthread_t queueThreadId1;
	pthread_attr_t queueAtr1;
	pthread_attr_init(&queueAtr1);
	pthread_create(&queueThreadId1, &queueAtr1, queue, (queueId)+1);

	pthread_t queueThreadId2;
	pthread_attr_t queueAtr2;
	pthread_attr_init(&queueAtr2);
	pthread_create(&queueThreadId2, &queueAtr2, queue, (queueId+2));

	// Create the student threads.
	struct studentStruct studentList[STUDENT_COUNT];
	int i;
	for (i = 0; i < STUDENT_COUNT; i++) {
		studentList[i].id = ID_BASE + i;
		studentList[i].priority = priorityGenerator();
		studentList[i].section = sectionGenerator();
		studentList[i].arrivalTime = 0;
		studentList[i].leaveTime = 0;

		pthread_t studentThreadId;
		pthread_attr_t studentAttr;
		pthread_attr_init(&studentAttr);
		pthread_create(&studentThreadId, &studentAttr, student, &studentList[i]);
	}

	// Wait for the queues to complete.
	pthread_join(queueThreadId, NULL);
	pthread_join(queueThreadId1, NULL);
	pthread_join(queueThreadId2, NULL);

	int o,p;
	//print number of students per section.
	for(p = 0; p < sectionCounts[0]; p++){
		int temp = section1[p].leaveTime - section1[p].arrivalTime;
		printf("Section %d has student ID: %d%s students, arrival time %d, leave time %d, turn around %d \n",
				1, section1[p].id, section1[p].priority,section1[p].arrivalTime, section1[p].leaveTime,temp );
	}
	printf("---\n");
	for(p = 0; p < sectionCounts[1]; p++){
		int temp = section2[p].leaveTime - section2[p].arrivalTime;
		printf("Section %d has student ID: %d%s students, arrival time %d, leave time %d, turn around %d \n",
				2, section2[p].id, section2[p].priority,section2[p].arrivalTime, section2[p].leaveTime,temp );
	}
	printf("---\n");
	for(p = 0; p < sectionCounts[2]; p++){
		int temp = section3[p].leaveTime - section3[p].arrivalTime;
		printf("Section %d has student ID: %d%s students, arrival time %d, leave time %d, turn around %d \n",
				3, section3[p].id, section3[p].priority,section3[p].arrivalTime, section3[p].leaveTime,temp );
	}
	printf("---\n");

	//print queues's turn around
	for(o = 0; o < NUMBER_OF_SECTION; o++){
		queueTurnAround[o] /= queueProcessed[o];
		printf("Queue %s has average turn around time of %.2f seconds\n", priority[o], queueTurnAround[o]);
	}

	//print students who were dropped
	for(o = 0; o < dropStudentCount; o++){
		printf("Student #%d%s was dropped.\n",droppedList[o].id,droppedList[o].priority);
	}

	//print impatient students who left
	for(o = 0; o < impatientStudentCount; o++){
		printf("Student #%d%s was impatient and left.\n",impatientList[o].id,impatientList[o].priority);
	}
	return 0;
}
