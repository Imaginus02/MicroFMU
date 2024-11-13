//#pragma once
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include "headers/fmi2TypesPlatform.h"
#include "headers/fmi2FunctionTypes.h"
#include "headers/fmi2Functions.h"
#include "fmu/sources/config.h"
#include "fmu/sources/model.h"
#include "fmi2.c"
#include "py/runtime.h"
//#include "fmu/sources/all.c"


//TODO: Make those values read by the Makefile

#define FMI_VERSION 2

#ifndef fmuGUID
#define fmuGUID "{1AE5E10D-9521-4DE3-80B9-D0EAAA7D5AF1}"
#endif

#ifndef DEBUG
#define INFO(message)
#else
#define INFO(message) printf(message)
#endif


#define min(a,b) ((a)>(b) ? (b) : (a))

FMU fmu;

char * fmi2StatusToString(fmi2Status status) {
	switch (status) {
		case fmi2OK: return "OK";
		case fmi2Warning: return "Warning";
		case fmi2Discard: return "Discard";
		case fmi2Error: return "Error";
		case fmi2Fatal: return "Fatal";
		case fmi2Pending: return "Pending";
		default: return "?";
	}
}


#define MAX_MSG_SIZE 1000
void fmuLogger (void *componentEnvironment, fmi2String instanceName, fmi2Status status,
               fmi2String category, fmi2String message, ...) {
	char msg[MAX_MSG_SIZE];
	//char *copy;
	va_list argp;

	va_start(argp, message);
	vsprintf(msg, message, argp);
	va_end(argp);

	if (instanceName == NULL) instanceName = "?";
	if (category == NULL) category = "?";
	printf("%s %s (%s): %s\n", fmi2StatusToString(status), instanceName, category, msg);
}

int error(const char *message) {
	printf("%s\n", message);
	return 0;
}


static int simulate(FMU *fmu, double tEnd, double h, fmi2Boolean loggingOn, int nCategories, const char * const* categories) {
	INFO("Entering simulate\n");

	int i;
	double dt, tPre;
	fmi2Boolean timeEvent, stateEvent, stepEvent, terminateSimulation;
	double time;
	int nx;                          // number of state variables
	int nz;                          // number of state event indicators
	double *x = NULL;                // continuous states
	double *xdot = NULL;             // the corresponding derivatives in same order
	double *z = NULL;                // state event indicators
	double *prez = NULL;             // previous values of state event indicators
	fmi2EventInfo eventInfo;         // updated by calls to initialize and eventUpdate
	fmi2CallbackFunctions callbacks = {fmuLogger, calloc, free, NULL, fmu}; // called by the model during simulation
	fmi2Component c;                 // instance of the fmu
	fmi2Status fmi2Flag;             // return code of the fmu functions
	fmi2Real tStart = 0;             // start time
	fmi2Boolean toleranceDefined = fmi2False; // true if model description define tolerance
	fmi2Real tolerance = 0;          // used in setting up the experiment
	fmi2Boolean visible = fmi2False; // no simulator user interface
	int nSteps = 0;
	int nTimeEvents = 0;
	int nStepEvents = 0;
	int nStateEvents = 0;

	INFO("Variables declared\n");

	// instantiate the fmu

	//fmi2Component fmi2Instantiate(fmi2String instanceName, fmi2Type fmuType, fmi2String fmuGUID,
    //                        fmi2String fmuResourceLocation, const fmi2CallbackFunctions *functions,
    //                        fmi2Boolean visible, fmi2Boolean loggingOn)
	c = fmu->instantiate("BouncingBall", fmi2ModelExchange, fmuGUID, NULL, &callbacks, visible, loggingOn);
	if (!c) return error("could not instantiate model");

	INFO("FMU instantiated\n");

	if (nCategories > 0) {
		fmi2Flag = fmu->setDebugLogging(c, fmi2True, nCategories, categories);
		if (fmi2Flag > fmi2Warning) {
			return error("could not initialize model; failed FMI set debug logging");
		}
	}

	INFO("Debug logging set\n");

	// allocate memory
	nx = getNumberOfContinuousStates(c);
	nz = getNumberOfEventIndicators(c);

	x = (double *) calloc(nx, sizeof(double));
	xdot = (double *) calloc(nx, sizeof(double));
	if (nz > 0) {
		z = (double *) calloc(nz, sizeof(double));
		prez = (double *) calloc(nz, sizeof(double));
	}
	if ((!x || !xdot) || (nz > 0 && (!z || !prez))) return error("out of memory");

	INFO("Memory allocated\n");

	//TODO: La création de l'output ne peut se faire comme ça, les variables sont de différents types
	
	//int **output = calloc(nCategories, sizeof(double*));
	//for (i=0; nCategories; i++) {
	//	output[i] = calloc(round((tEnd-tStart)/h), sizeof(double));
	//}

	// setup
	time = tStart;
	fmi2Flag = fmu->setupExperiment(c, toleranceDefined, tolerance, tStart, fmi2True, tEnd);
	if (fmi2Flag > fmi2Warning) {
		return error("could not initialize model; failed FMI setup experiment");
	}

	INFO("Experiment setup\n");

	// initialize
	fmi2Flag = fmu->enterInitializationMode(c);
	if (fmi2Flag > fmi2Warning) {
		return error("could not initialize model; failed FMI enter initialization mode");
	}
	fmi2Flag = fmu->exitInitializationMode(c);
	if (fmi2Flag > fmi2Warning) {
		return error("could not initialize model; failed FMI exit initialization mode");
	}

	INFO("Initialization mode done\n");

	// event iteration
	eventInfo.newDiscreteStatesNeeded = fmi2True;
	eventInfo.terminateSimulation = fmi2False;
	while (eventInfo.newDiscreteStatesNeeded && !eventInfo.terminateSimulation) {
		// update discrete states
		fmi2Flag = fmu->newDiscreteStates(c, &eventInfo);
		if (fmi2Flag > fmi2Warning) return error("could not set a new discrete state");
	}

	INFO("Event iteration done\n");

	if (eventInfo.terminateSimulation) {
		printf("model requested termination at t=%.16g\n", time);
	} else {
		// enter Continuous-Time Mode
		fmi2Flag = fmu->enterContinuousTimeMode(c);
		if (fmi2Flag > fmi2Warning) return error("could not initialize model; failed FMI enter continuous time mode");

		INFO("Continuous time mode entered\n");
		// TODO: Handle the ouput of a row of values
		//output[0][nSteps]=time;

		// On a absolument besoin du type de la variable car sinon on ne peut pas accéder à ses valeurs
		// Ici c'est un workaround pour le moment car on utilise BouncingBall  dont on connait les types
		//fmu->getReal(c, 0, 1, &output[1][nSteps]);


		// enter the simulation loop
		while (time < tEnd) {
			INFO("Entering simulation loop\n");
			// get the current state and derivatives
			fmi2Flag = fmu->getContinuousStates(c, x, nx);
			if (fmi2Flag > fmi2Warning) return error("could not retrieve states");

			fmi2Flag = fmu->getDerivatives(c, xdot, nx);
			if (fmi2Flag > fmi2Warning) return error("could not retrieve derivatives");

			INFO("States and derivatives retrieved\n");

			// advance time
			tPre = time;
			time = min(time + h, tEnd);
			timeEvent = eventInfo.nextEventTimeDefined && time >= eventInfo.nextEventTime;
			if (timeEvent) time = eventInfo.nextEventTime;
			dt = time - tPre;
			fmi2Flag = fmu->setTime(c, time);
			if (fmi2Flag > fmi2Warning) return error("could not set time");

			INFO("Time set\n");

			// perform one step
			for (i = 0; i < nx; i++) x[i] += dt * xdot[i]; // forward Euler method
			fmi2Flag = fmu->setContinuousStates(c, x, nx);
			if (fmi2Flag > fmi2Warning) return error("could not set continuous states");
			if (loggingOn) printf("time = %g\n", time);

			INFO("Step performed\n");

			// check for state event
			for (i = 0; i < nz; i++) prez[i] = z[i];
			fmi2Flag = fmu->getEventIndicators(c, z, nz);
			if (fmi2Flag > fmi2Warning) return error("could not retrieve event indicators");
			stateEvent = fmi2False;
			for (i = 0; i < nz; i++) stateEvent = stateEvent || (prez[i] * z[i] < 0);

			INFO("State event checked\n");

			// check for step event
			fmi2Flag = fmu->completedIntegratorStep(c, fmi2True, &stepEvent, &terminateSimulation);
			if (fmi2Flag > fmi2Warning) return error("could not complete integrator step");
			if (terminateSimulation) {
				printf("model requested termination at t=%.16g\n", time);
				break; // success
			}

			INFO("Step event checked\n");

			// handle events
			if (timeEvent || stateEvent || stepEvent) {
				INFO("Event detected\n");
				fmi2Flag = fmu->enterEventMode(c);
				if (fmi2Flag > fmi2Warning) return error("could not enter event mode");

				if (timeEvent) {
					INFO("Time event\n");
					nTimeEvents++;
					if (loggingOn) printf("time event at t=%.16g\n", time);
				}
				if (stateEvent) {
					INFO("State event\n");
					nStateEvents++;
					if (loggingOn) for (i = 0; i < nz; i++) printf("state event %s z[%d] at t=%.16g\n", (prez[i] > 0 && z[i] < 0) ? "-\\-" : "-/-", i, time);
				}
				if (stepEvent) {
					INFO("Step event\n");
					nStepEvents++;
					if (loggingOn) printf("step event at t=%.16g\n", time);
				}
				INFO("Event handled\n");

				// event iteration in one step, ignoring intermediate results
				eventInfo.newDiscreteStatesNeeded = fmi2True;
				eventInfo.terminateSimulation = fmi2False;
				while (eventInfo.newDiscreteStatesNeeded && !eventInfo.terminateSimulation) {
					// update discrete states
					fmi2Flag = fmu->newDiscreteStates(c, &eventInfo);
					if (fmi2Flag > fmi2Warning) return error("could not set a new discrete state");

					// check for change of value of states
					if (eventInfo.valuesOfContinuousStatesChanged && loggingOn) {
						printf("continuous state values changed at t=%.16g\n", time);
					}
					if (eventInfo.nominalsOfContinuousStatesChanged && loggingOn) {
						printf("nominals of continuous state changed  at t=%.16g\n", time);
					}
				}
				if (eventInfo.terminateSimulation) {
					printf("model requested termination at t=%.16g\n", time);
					break; // success
				}

				// enter Continuous-Time Mode
				fmi2Flag = fmu->enterContinuousTimeMode(c);
				if (fmi2Flag > fmi2Warning) return error("could not enter continuous time mode");
			} // if event
			// TODO: Output values for this step
			nSteps++;
		} // while
	}
	// cleanup
	fmi2Flag = fmu->terminate(c);
	if (fmi2Flag > fmi2Warning) return error("could not terminate model");
	
	fmu->freeInstance(c);
	if (x != NULL) free(x);
    if (xdot != NULL) free(xdot);
    if (z != NULL) free(z);
    if (prez != NULL) free(prez);

	//print simulation summary
	    // print simulation summary
    printf("Simulation from %g to %g terminated successful\n", tStart, tEnd);
    printf("  steps ............ %d\n", nSteps);
    printf("  fixed step size .. %g\n", h);
    printf("  time events ...... %d\n", nTimeEvents);
    printf("  state events ..... %d\n", nStateEvents);
    printf("  step events ...... %d\n", nStepEvents);

    return 1; // success
};

// int main(int argc, char *argv[]) {
// 	double tEnd = 3;
// 	double h = 0.01;
// 	int loggingOn;
// 	int nCategories=4+1; //TODO: This should come from a struct that is created by the makefile and read from the xml file
// 	char **logCategories;

// 	loadFunctions(&fmu);

// 	simulate(&fmu, tEnd, h, loggingOn, nCategories, logCategories);

// 	return 0;
// }

// Define a function :
static mp_obj_t example_simulate(mp_obj_t end_time, mp_obj_t step_size) {
	//double tStart = mp_obj_get_float(start_time);
	//printf("tStart: %f\n", tStart);
	double tEnd = mp_obj_get_float(end_time);
	double h = mp_obj_get_float(step_size);

	int loggingOn = 0;
	int nCategories=0; 
	//"logAll", "logError", "logFmiCall", "logEvent", "logStep"
	const char * const* logCategories = NULL;

	loadFunctions(&fmu);

	simulate(&fmu, tEnd, h, loggingOn, nCategories, logCategories);

	return mp_obj_new_int(1);
}

// On permet l'appel de cette fonction dans python :
static MP_DEFINE_CONST_FUN_OBJ_2(example_simulate_obj, example_simulate);

// On va mapper les noms des variables et des class :
static const mp_rom_map_elem_t example_module_globals_table[] = {
	{ MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_testlibrary)},
	{ MP_ROM_QSTR(MP_QSTR_simulate), MP_ROM_PTR(&example_simulate_obj)},
};
static MP_DEFINE_CONST_DICT(example_module_globals, example_module_globals_table);

//On définit le module :
const mp_obj_module_t example_user_testlibrary = {
	.base = { &mp_type_module },
	.globals= (mp_obj_dict_t *)&example_module_globals
};

// Enregistrement le module pour le rendre accessible sous python :
MP_REGISTER_MODULE(MP_QSTR_testlibrary, example_user_testlibrary);
