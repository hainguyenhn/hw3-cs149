#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <sys/time.h>

int allSectionFull();

#define ID_BASE 101

#define CHAIR_COUNT 3
#define STUDENT_COUNT 10
#define SECTION_COUNT 3

#define NUMBER_OF_SECTION 3
#define SECTION_CAPACITY 20

#define MAX_MEETING_DURATION 5
#define OFFICE_HOUR_DURATION 60


struct studentStruct{
	int id;
	char* priority;
	int section;
	time_t arrivalTime;
	time_t leaveTime;
};

// An example multithreaded program
// by Ron Mak
// Department of Computer Science
// San Jose State University
//
// WARNING: Contains a subtle threading error which can cause a deadlock!
//          Can you find and fix it?
char* priority[] = {"GS","RS","EE"};
int chairs[CHAIR_COUNT];     // circular buffer of chairs
pthread_mutex_t chairMutex;  // mutex protects chairs and wait count
pthread_mutex_t GsQueueMutex;  // mutex protects chairs and wait count
pthread_mutex_t RsQueueMutex;  // mutex protects chairs and wait count
pthread_mutex_t EeQueueMutex;  // mutex protects chairs and wait count
pthread_mutex_t sectionMutex;  // mutex protects chairs and wait count

pthread_mutex_t printMutex;  // mutex protects printing

sem_t GsQueueSem;
sem_t RsQueueSem;
sem_t EeQueueSem;

sem_t filledChairs;          // professor waits on this semaphore



struct itimerval profTimer;  // professor's office hour timer
time_t startTime;

int in = 0, out = 0;
int meetingId = 0;

int arrivalsCount = 0;
int waitCount = 0;
int leavesCount = 0;
int meetingsCount = 0;
int parforeCount = 0;


int section1[SECTION_CAPACITY];
int section2[SECTION_CAPACITY];
int section3[SECTION_CAPACITY];

int sectionCounts[3] = {0,0,0};

//keep track of number of students each queue processed.
int queueProcessed[3] = {0,0,0};

//keep track of student
struct studentStruct droppedList[STUDENT_COUNT];
int dropStudentCount = 0;

//keep each queue's turn around time.
double queueTurnAround[3] = {0,0,0};

//keep track of number of student in each queue.
int GsQueuePOS = 0;
int RsQueuePOS = 0;
int EeQueuePOS = 0;

//total number of students has been processed.
int studentProcessed = 0;

int firstPrint = 1;



// Print a line for each event:
//   elapsed time
//   who is meeting with the professor
//   who is waiting in the chairs
//   what event occurred

int  completed();
void closeAllQueue();



struct studentStruct GsQueue[STUDENT_COUNT];
struct studentStruct RsQueue[STUDENT_COUNT];
struct studentStruct EeQueue[STUDENT_COUNT];
struct studentStruct sections[NUMBER_OF_SECTION][SECTION_CAPACITY];


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

	// Who's meeting with the professor.
	if (meetingId > 0) {
		printf("%10d   |", meetingId);
	}
	else {
		printf("        |");
	}

	// Acquire the mutex lock to protect the chairs and the wait count.
	pthread_mutex_lock(&chairMutex);

	int i = out;
	int j = waitCount;
	int k = 0;

	// Who's waiting in the chairs.
	while (j-- > 0) {
		printf("%4d", chairs[i]);
		i = (i+1)%CHAIR_COUNT;
		k++;
	}

	// Release the mutex lock.
	pthread_mutex_unlock(&chairMutex);

	// What event occurred.
	while (k++ < CHAIR_COUNT) printf("    ");
	printf(" | %s\n", event);

	// Release the mutex lock.
	pthread_mutex_unlock(&printMutex);
}

// A student arrives.
void studentArrives(struct studentStruct* student)
{
	int id = (int)student->id;
	char* priority = (char*) student->priority;
	int section = (int)student->section;
	char event[100];
	arrivalsCount++;

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
	}

}

// The student thread.
void *student(struct studentStruct *student)
{
	// Students will arrive at random times during the office hour.
	sleep(rand()%5+0.1);

	time_t currentTime;
	time(&currentTime);
	student->arrivalTime = currentTime;
	studentArrives(student);

	return NULL;
}

int timesUp = 0;  // 1 = office hour is over

// The professor meets a student (or works on ParFore).
void professorMeetsStudent(int queueNum)
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
				sprintf(event, "Student #%d.%s is enrolled into Section %d",  temp -> id, temp -> priority, enroll );
				printf("Section %d has %d students\n", enroll + 1, sectionCounts[enroll]);
				print(event);
			}
			else{
				sprintf(event, "Student #%d.%s is dropped",  temp -> id, temp -> priority);
				droppedList[dropStudentCount++] = *(temp);
				print(event);
			}
			studentProcessed++;
			time_t leave;
			time(&leave);
			temp->leaveTime = leave;
		}

		break;

	case 2:
		sem_wait(&RsQueueSem);
		if((allSectionFull() != 1) && (studentProcessed < STUDENT_COUNT) && (queueProcessed[1] < RsQueuePOS)){

			pthread_mutex_lock(&RsQueueMutex);
			struct studentStruct* temp = &RsQueue[queueProcessed[1]++];
			pthread_mutex_unlock(&RsQueueMutex);
			char event[80];
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
				sprintf(event, "Student #%d.%s is enrolled into Section %d",  temp -> id, temp -> priority, enroll1 );
				printf("Section %d has %d students\n", enroll1 + 1, sectionCounts[enroll1]);
				print(event);
			}
			else{
				sprintf(event, "Student #%d.%s is dropped",  temp -> id, temp -> priority);
				droppedList[dropStudentCount++] = *(temp);
				print(event);
			}
			studentProcessed++;
			time_t leave;
			time(&leave);
			temp->leaveTime = leave;

		}
		break;

	case 3:
		sem_wait(&EeQueueSem);
		if((allSectionFull() != 1) && (studentProcessed < STUDENT_COUNT) && (queueProcessed[2] < EeQueuePOS)){

			pthread_mutex_lock(&EeQueueMutex);
			struct studentStruct* temp = &EeQueue[queueProcessed[2]++];
			pthread_mutex_unlock(&EeQueueMutex);
			char event[80];
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
				sprintf(event, "Student #%d.%s is enrolled into Section %d",  temp -> id, temp -> priority, enroll2 );
				printf("Section %d has %d students\n", enroll2 + 1, sectionCounts[enroll2]);
				print(event);
			}
			else{
				sprintf(event, "Student #%d.%s is dropped",  temp -> id, temp -> priority);
				droppedList[dropStudentCount++] = *(temp);
				print(event);
			}
			studentProcessed++;
			time_t leave;
			time(&leave);
			temp->leaveTime = leave;

		}
		break;
	}}


// The professor thread.
void *professor(int param)
{
	int queue =  param;
	switch(queue){
	case 1:
		print("GS Queue is open");
		do{

			professorMeetsStudent(1);
		} while (completed() != 1);
		print("GS Queue is closed");
		closeAllQueue();
		return NULL;
		break;
	case 2:
		print("RS Queue is open");
		do{

			professorMeetsStudent(2);
		} while (completed() != 1);
		print("RS Queue is closed");
		closeAllQueue();
		return NULL;
		break;
	case 3:
		print("EE Queue is open");
		do{

			professorMeetsStudent(3);
		} while  (completed() != 1);
		print("EE Queue is closed");
		closeAllQueue();
		return NULL;
		break;
	}
}

// Timer signal handler.
void timerHandler(int signal)
{
	timesUp = 1;  // office hour is over
}

//random Priority Generator
char* priorityGenerator(){
	char* priority[] = {"GS","RS","EE"};
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
// section 0 meaning student can enroll any sections 1,2,3 with equal probability.
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

int allSectionFull(){
	int i;
	for(i = 0; i < NUMBER_OF_SECTION; i++){
		if(sectionCounts[i] < SECTION_CAPACITY){
			return 0;
		}
	}
	return 1;
}

void closeAllQueue(){
	sem_post(&GsQueueSem);
	sem_post(&RsQueueSem);
	sem_post(&EeQueueSem);
}

int completed(){
	if((studentProcessed >= STUDENT_COUNT) || (allSectionFull() == 1)){
		return 1;
	}
	else{
		return 0;
	}

}

int enrollStudent(struct studentStruct* student){
	pthread_mutex_lock(&sectionMutex);
	int studentSec = student -> section;
	if(studentSec == 0){
		int i;
		for(i = 0; i < NUMBER_OF_SECTION; i++){
			if(sectionCounts[i] < SECTION_CAPACITY){
				studentSec = i;
			}
		}
	}
	//because array start at 0.
	studentSec--;
	if(sectionCounts[studentSec] < SECTION_CAPACITY){
		sections[studentSec][sectionCounts[studentSec]++] = *(student);
		pthread_mutex_unlock(&sectionMutex);
		return studentSec;
	}
	else{
		pthread_mutex_unlock(&sectionMutex);
		return -1;
	}
}


// Main.
int main(int argc, char *argv[])
{
	int professorId = 1;

	// Initialize the mutexes and the semaphore.
	pthread_mutex_init(&chairMutex, NULL);
	pthread_mutex_init(&printMutex, NULL);
	pthread_mutex_init(&GsQueueMutex, NULL);
	pthread_mutex_init(&RsQueueMutex, NULL);
	pthread_mutex_init(&EeQueueMutex, NULL);
	pthread_mutex_init(&sectionMutex, NULL);

	sem_init(&filledChairs, 0, 0);
	sem_init(&GsQueueSem, 0, 0);
	sem_init(&RsQueueSem, 0, 0);
	sem_init(&EeQueueSem, 0, 0);


	srand(time(0));
	time(&startTime);

	// Create the professor thread.
	pthread_t professorThreadId;
	pthread_attr_t profAttr;
	pthread_attr_init(&profAttr);
	pthread_create(&professorThreadId, &profAttr, professor, professorId);

	pthread_t professorThreadId1;
	pthread_attr_t profAttr1;
	pthread_attr_init(&profAttr1);
	pthread_create(&professorThreadId1, &profAttr1, professor, professorId+1);

	// Create the professor thread.
	pthread_t professorThreadId2;
	pthread_attr_t profAttr2;
	pthread_attr_init(&profAttr2);
	pthread_create(&professorThreadId2, &profAttr2, professor, professorId+2);


	struct studentStruct studentList[STUDENT_COUNT];
	// Create the student threads.
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

	// Set the timer signal handler.
	signal(SIGALRM, timerHandler);

	// Wait for the professor to complete the office hour.
	pthread_join(professorThreadId, NULL);

	int o,p;
	for (o = 0; o < SECTION_COUNT;o++){
		for(p = 0; p < sectionCounts[o]; p++){
			int temp = sections[o][p].leaveTime - sections[o][p].arrivalTime;
			printf("Section %d has student ID: %d%s students, arrival time %d, leave time %d, turn around %d \n",
					o+1, sections[o][p].id, sections[o][p].priority,sections[o][p].arrivalTime, sections[o][p].leaveTime,temp );

		}
		printf("---\n");
	}

	for(o = 0; o < SECTION_COUNT; o++){
		queueTurnAround[o] /= queueProcessed[o];
		printf("Queue %s has average turn around time of %.2f seconds\n", priority[0], queueTurnAround[o]);
	}

	for(o = 0; o < dropStudentCount; o++){
		printf("Student #%d%s was dropped.\n",droppedList[i].id,droppedList[i].priority);
	}

	return 0;
}
