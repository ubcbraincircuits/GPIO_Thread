
#include "SimpleGPIO_CountermandPulse.h"

/* A pulsedThread (or subclass) pulse waits for the set delay, runs the high function, waits for the set duration,
 and runs the low function. This countermandable GPIO_thread pulse can be countermanded, i.e., cancelled,
 if and only if the countermand function is run while a pulse is waiting for the delay period.
 Before a pulse is requested, or after the high function has already run, countermanding has no effect.
 To do this, we add a field, countermand, to the GPIO_thread taskData. countermand is  0 when no pulse is active,
 1 when a pulse is waiting for delay and can be countermanded, 2 when waiting for delay and a countermand is requested,
 3 when not countermanded and waiting for duration, and 4 when countermaned and waiting for duration.
 We also limit when countermandable pulses can be started to times when a pulse is not currently active, so there is
 no confusion as to which pulse in a series is supposed to be countermanded. To start a countermandable pulse,
 call doCountermandPulse instead of DoTask. doCountermandPulse will only start a pulse if there is no pulse
 currently in progress. It returns a boolean, true if a pulse was actually started as requested else false.
 You can start a CountermandPulse with DoTask or DoTasks, but then the pulse or pulses won't be countermandable. */

 /* **************** countermandable Hi Callback ******************************
Task to do on High tick, sets GPIO line high or low depending on polarity, unless countermanded
Sets cmpTaskPtr->wasCountermanded as appropriate
last modified:
2018/05/23 by Jamie Boyd - added taskMutexPtr to prevent simultaneous access by both thread and class method
2018/04/04 by Jamie Boyd - added Lo func and wasCountermanded boolean
2018/03/16 by Jamie Boyd - initial version */
void Countermand_Hi (void *  taskData){
	CountermandPulseStructPtr cmpTaskPtr = (CountermandPulseStructPtr) taskData;
	//pthread_mutex_lock (cmpTaskPtr->taskMutexPtr);
	if (cmpTaskPtr->countermand == 2){ // countermand requested
		cmpTaskPtr->countermand = 4; // set countermand in progress
		cmpTaskPtr->wasCountermanded= true;
	}else{ // no countermand requested
		*(cmpTaskPtr->GPIOperiHi) = cmpTaskPtr->pinBit; // no countermand so set high
		cmpTaskPtr->countermand = 3; // set no countermand in progress
		cmpTaskPtr->wasCountermanded= false;
	}
	//pthread_mutex_unlock (cmpTaskPtr->taskMutexPtr);
}

/* ********************************** countermandable low callback *****************************************************
if not countermanded, sets GPIO output low. Sets cmpTaskPtr->countermand back to 0
 last modified:
 2018/04/04 by Jamie Boyd - initial version */
void Countermand_Lo (void *  taskData){
	CountermandPulseStructPtr cmpTaskPtr = (CountermandPulseStructPtr) taskData;
	if (cmpTaskPtr->countermand != 4){ // if 4, pulse was countermanded, no need to set low
		*(cmpTaskPtr->GPIOperiLo) = cmpTaskPtr->pinBit;
	}
	cmpTaskPtr->countermand = 0; // reset countermand to 0
}


/* ***********************************initialization function, copied from SImpleGPIO with additions *******************************
last modified:
2018/10/17 by Jamie Boyd - copied modFunction into init function */
int countermand_Init (void * initDataP, void *  &taskDataP){
	// task data pointer is a void pointer that needs to be initialized to a pointer to taskData and filled from our custom init structure 
	CountermandPulseStructPtr taskData= new CountermandPulseStruct;
	taskDataP = taskData;
	// initData is a pointer to SimpleGPIO custom init structure
	SimpleGPIOInitStructPtr initDataPtr = (SimpleGPIOInitStructPtr) initDataP;
	// calculate address to ON and OFF register as Hi or Lo as appropriate to save a level of indirection later
	if (initDataPtr->thePolarity == 1){ // High to Low pulses
		taskData->GPIOperiHi = (unsigned int *) (initDataPtr->GPIOperiAddr + 10);
		taskData->GPIOperiLo = (unsigned int *) initDataPtr->GPIOperiAddr+ 7;
	}else{ // low to high pulses
		taskData->GPIOperiHi = (unsigned int *) (initDataPtr->GPIOperiAddr + 7);
		taskData->GPIOperiLo = (unsigned int *) (initDataPtr->GPIOperiAddr + 10);
	}
	// calculate pinBit
	taskData->pinBit =  1 << initDataPtr->thePin;
	// initialize pin for output
	*(initDataPtr->GPIOperiAddr + ((initDataPtr->thePin) /10)) &= ~(7<<(((initDataPtr->thePin) %10)*3));
	*(initDataPtr->GPIOperiAddr + ((initDataPtr->thePin)/10)) |=  (1<<(((initDataPtr->thePin)%10)*3));
	// put pin in selected start state
	*(taskData->GPIOperiLo ) = taskData->pinBit ;
	// init countermanded variables 
	taskData->countermand = 0;
	taskData->wasCountermanded = false;
	delete initDataPtr;
	printf ("Init func ran.\n"); 
	return 0; // 
}


/* ****************************** Custom delete Function *****************************************************/
void CountermandPulse_delTask (void * taskData){
	delete (CountermandPulseStructPtr) taskData;
}


/* *************** Thread maker makes SimpleGPIO init data struct, calls constructor, and does some postcreation modification *************
Last Modified:
2018/04/04 by Jamie Boyd - added modCustom for setting extra data
2018/03/16 by Jamie Boyd  - initial version */
CountermandPulse * CountermandPulse::CountermandPulse_threadMaker (int pin, int polarity, unsigned int delayUsecs, unsigned int durUsecs, int accuracyLevel){
	// make and fill an init struct for SimpleGPIO
	SimpleGPIOInitStructPtr initStruct = new SimpleGPIOInitStruct;
	initStruct->thePin = pin;
	initStruct->thePolarity = polarity;
	initStruct->GPIOperiAddr =  useGpioPeri ();
	if (initStruct->GPIOperiAddr == nullptr){
#if beVerbose
		printf ("CountermandPulse_threadMaker failed to map GPIO peripheral.\n");
#endif
		return nullptr;
	}
	int errCode =0;
	// call CountermandPulse constructor, which calls SimpleGPIO_thread contructor, which calls pulsedThread constructor
	CountermandPulse * newCountermandPulse = new CountermandPulse (pin, polarity, delayUsecs, durUsecs, (void *) initStruct, accuracyLevel, errCode);
	if (errCode){
#if beVerbose
		printf ("CountermandPulse_threadMaker Failed to make pulsed thread.\n");
#endif
		return nullptr;
	}
	// set countermanable high and lo functions in place of regular hi and lo functions
	newCountermandPulse->setHighFunc (&Countermand_Hi);
	newCountermandPulse->setLowFunc (&Countermand_Lo);
	// set custom delete function for task data
	newCountermandPulse->setTaskDataDelFunc (&CountermandPulse_delTask);
	// make a task pointer for easy direct access to thread task data
	newCountermandPulse->taskStructPtr = (CountermandPulseStructPtr)newCountermandPulse->getTaskData ();
	// get pointer to the task, and  copy pointer to mutex  from the task itself into task data
	taskParams * theTask = newCountermandPulse->getTask ();
	newCountermandPulse->taskStructPtr ->taskMutexPtr = &theTask->taskMutex;
	return newCountermandPulse;
}

//countermand =0 when no pulse is active, 1 when a pulse is waiting for delay and can be countermanded,
//2 when waiting for delay and countermand is requested, 3 when not countermanded and waiting for duration,
// 4 when countermaned and waiting for duration

bool CountermandPulse::doCountermandPulse (void){
	pthread_mutex_lock (&theTask.taskMutex);
    if (taskStructPtr->countermand== 0){
        taskStructPtr->countermand= 1;
		pthread_mutex_unlock (&theTask.taskMutex);
        DoTask();
        return true;
    }else{
		pthread_mutex_unlock (&theTask.taskMutex);
        return false;
    }
}

/* *************************** to countermand, just set countermand in the task pointer to 1, and current pulse will be countermanded
IF countermand is received before delay is up. Returns true if pulse is in countermandable state, that is, waiting for duration
Last Modified:
2018/05/23 by jamie Boyd - added taskMutexPtr to prevent simultaneous access by both thread and class
2018/04/04 by Jamie Boyd - initial version*/
bool CountermandPulse::countermand (void){
	bool returnVal = false;
	pthread_mutex_lock (&theTask.taskMutex);
	if (taskStructPtr->countermand== 1){
		taskStructPtr->countermand = 2;
		returnVal = true;
	}
	pthread_mutex_unlock (&theTask.taskMutex);
	return returnVal;
}

/* ************************ returns truth that last pulse was countermanded ********************************
Last Modified:
2018/04/04 by Jamie Boyd - initial version */
bool CountermandPulse::wasCountermanded (void){
	return taskStructPtr->wasCountermanded;
}
