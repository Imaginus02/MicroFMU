#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include "headers/fmi2TypesPlatform.h"
#include "headers/fmi2FunctionTypes.h"
#include "headers/fmi2Functions.h"
#include "fmu/sources/config.h"
#include "fmu/sources/model.h"

//Bibliothèque pour l'implémentation en micropython
#include "py/obj.h"
#include "py/runtime.h"

//Fichier C créé pour le simulateur
#include "fmi2.c"
#include "output.c"


//TODO: Make those values read by the Makefile

#define FMI_VERSION 2

#ifndef fmuGUID
#define fmuGUID "{1AE5E10D-9521-4DE3-80B9-D0EAAA7D5AF1}"
#endif

//Affichage de messages supplémentaire si le mode debug est activé lors de la compilation avec le flag -DDEBUG
#ifndef DEBUG
#define INFO(message, ...)
#else
#define INFO(message, ...) do { \
	char buffer[256]; \
	snprintf(buffer, sizeof(buffer), message, ##__VA_ARGS__); \
	printf("%s", buffer); \
} while (0)
#endif

//Fonction minimum de deux objets
#define min(a,b) ((a)>(b) ? (b) : (a))


// Structure to hold simulation state
typedef struct {
    fmi2Component component;
    int nx;                          // number of state variables
    int nz;                          // number of state event indicators
    double *x;                       // continuous states
    double *xdot;                    // derivatives
    double *z;                       // state event indicators
    double *prez;                    // previous state event indicators
    double time;                     // current simulation time
    double h;                        // step size
    double tEnd;                     // end time
    fmi2EventInfo eventInfo;         // event info
    ScalarVariable *variables;       // model variables
    int nVariables;                  // number of variables
    double *output;                 // output array
    int nSteps;                      // current step count
    int nTimeEvents;                 // number of time events
    int nStateEvents;                // number of state events
    int nStepEvents;                 // number of step events
    fmi2Boolean loggingOn;          // logging flag
} SimulationState;


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


static int simulate(FMU *fmu,double tStart, double tEnd, double h) {
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
	fmi2Boolean toleranceDefined = fmi2False; // true if model description define tolerance
	fmi2Real tolerance = 0;          // used in setting up the experiment
	fmi2Boolean visible = fmi2False; // no simulator user interface
	int nSteps = 0;
	int nTimeEvents = 0;
	int nStepEvents = 0;
	int nStateEvents = 0;

	ScalarVariable * variables;
	int nVariables;

	INFO("Variables declared\n");

	// instantiate the fmu

	//fmi2Component fmi2Instantiate(fmi2String instanceName, fmi2Type fmuType, fmi2String fmuGUID,
    //                        fmi2String fmuResourceLocation, const fmi2CallbackFunctions *functions,
    //                        fmi2Boolean visible, fmi2Boolean loggingOn)
	c = fmu->instantiate("BouncingBall", fmi2ModelExchange, fmuGUID, NULL, &callbacks, visible, fmi2False);
	if (!c) return error("could not instantiate model");

	INFO("FMU instantiated\n");

	// if (nCategories > 0) {
	// 	fmi2Flag = fmu->setDebugLogging(c, fmi2True, nCategories, categories);
	// 	if (fmi2Flag > fmi2Warning) {
	// 		return error("could not initialize model; failed FMI set debug logging");
	// 	}
	// }

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
	
	get_variable_list(&variables);
	nVariables = get_variable_count();
	// Initialize the output array
	double **output = (double **)calloc(nVariables + 1, sizeof(double *));
	for (i = 0; i < nVariables + 1; i++) {
		output[i] = (double *)calloc((size_t)(tEnd/h+10), sizeof(double)); //Au cas où +10, normallement on devrait pas en avoir besoin TODO: Vérifier
	}

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
		// Initialize the output array with variable values
		INFO("Initializing output array for %d variables\n", nVariables);
		for (i = 0; i < nVariables-1; i++) {
			printf("Getting variable %s\n", variables[i].name);
			if (variables[i].type == REAL) {
				fmu->getReal(c, &variables[i].valueReference, 1, &output[i + 1][0]);
			} else if (variables[i].type == INTEGER) {
				fmi2Integer intValue;
				fmu->getInteger(c, &variables[i].valueReference, 1, &intValue);
				output[i + 1][0] = (double)intValue;
			}
		}


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
					//if (loggingOn) printf("time event at t=%.16g\n", time);
				}
				if (stateEvent) {
					INFO("State event\n");
					nStateEvents++;
					//if (loggingOn) for (i = 0; i < nz; i++) printf("state event %s z[%d] at t=%.16g\n", (prez[i] > 0 && z[i] < 0) ? "-\\-" : "-/-", i, time);
				}
				if (stepEvent) {
					INFO("Step event\n");
					nStepEvents++;
					//if (loggingOn) printf("step event at t=%.16g\n", time);
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
					// if (eventInfo.valuesOfContinuousStatesChanged && loggingOn) {
					// 	printf("continuous state values changed at t=%.16g\n", time);
					// }
					// if (eventInfo.nominalsOfContinuousStatesChanged && loggingOn) {
					// 	printf("nominals of continuous state changed  at t=%.16g\n", time);
					// }
				}
				if (eventInfo.terminateSimulation) {
					printf("model requested termination at t=%.16g\n", time);
					break; // success
				}

				// enter Continuous-Time Mode
				fmi2Flag = fmu->enterContinuousTimeMode(c);
				if (fmi2Flag > fmi2Warning) return error("could not enter continuous time mode");
			} // if event
			// Get the variable values for this step
			for (i = 0; i < nVariables-1; i++) {
				if (variables[i].type == REAL) {
					fmu->getReal(c, &variables[i].valueReference, 1, &output[i + 1][nSteps]);
				} else if (variables[i].type == INTEGER) {
					fmi2Integer intValue;
					fmu->getInteger(c, &variables[i].valueReference, 1, &intValue);
					output[i + 1][nSteps] = (double)intValue;
				}
			}
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

	//print the output
	for (i = 0; i < nVariables - 1; i++) {
		printf("%s: ", variables[i+1].name);
		for (int j = 0; j < nSteps; j++) {
			printf("%f ", output[i+1][j]);
		}
		printf("\n");
	}
	
	//free the output
	for (i = 0; i < nVariables + 1; i++) {
		free(output[i]);
	}
	free(output);

    return 1; // success
};

SimulationState* initializeSimulation(FMU *fmu, double tStart, double tEnd, double h) {
    SimulationState *state = (SimulationState*)calloc(1, sizeof(SimulationState));
    if (!state) return NULL;

    state->time = tStart;
    state->h = h;
    state->tEnd = tEnd;
    state->nSteps = 0;
    state->nTimeEvents = 0;
    state->nStateEvents = 0;
    state->nStepEvents = 0;

    // Setup callback functions
    fmi2CallbackFunctions callbacks = {fmuLogger, calloc, free, NULL, fmu};

    // Instantiate the FMU
    state->component = fmu->instantiate("BouncingBall", fmi2ModelExchange, 
                                      fmuGUID, NULL, &callbacks, fmi2False, fmi2False);
    if (!state->component) {
        free(state);
        return NULL;
    }

    // Get state dimensions
    state->nx = getNumberOfContinuousStates(state->component);
    state->nz = getNumberOfEventIndicators(state->component);

    // Allocate memory for states and indicators
    state->x = (double*)calloc(state->nx, sizeof(double));
    state->xdot = (double*)calloc(state->nx, sizeof(double));
    if (state->nz > 0) {
        state->z = (double*)calloc(state->nz, sizeof(double));
        state->prez = (double*)calloc(state->nz, sizeof(double));
    }

    if ((!state->x || !state->xdot) || 
        (state->nz > 0 && (!state->z || !state->prez))) {
        // Cleanup and return on allocation failure
        if (state->x) free(state->x);
        if (state->xdot) free(state->xdot);
        if (state->z) free(state->z);
        if (state->prez) free(state->prez);
        fmu->freeInstance(state->component);
        free(state);
        return NULL;
    }

    // Setup experiment
    fmi2Boolean toleranceDefined = fmi2False;
    fmi2Real tolerance = 0;
    fmi2Status fmi2Flag = fmu->setupExperiment(state->component, toleranceDefined, 
                                              tolerance, state->time, fmi2True, tEnd);
    if (fmi2Flag > fmi2Warning) {
        // Cleanup and return on setup failure
        fmu->freeInstance(state->component);
        free(state);
        return NULL;
    }

    // Initialize the FMU
    fmi2Flag = fmu->enterInitializationMode(state->component);
    if (fmi2Flag > fmi2Warning) {
        fmu->freeInstance(state->component);
        free(state);
        return NULL;
    }

    fmi2Flag = fmu->exitInitializationMode(state->component);
    if (fmi2Flag > fmi2Warning) {
        fmu->freeInstance(state->component);
        free(state);
        return NULL;
    }

    // Initial event iteration
    state->eventInfo.newDiscreteStatesNeeded = fmi2True;
    state->eventInfo.terminateSimulation = fmi2False;
    while (state->eventInfo.newDiscreteStatesNeeded && 
           !state->eventInfo.terminateSimulation) {
        fmi2Flag = fmu->newDiscreteStates(state->component, &state->eventInfo);
        if (fmi2Flag > fmi2Warning) {
            fmu->freeInstance(state->component);
            free(state);
            return NULL;
        }
    }

    if (!state->eventInfo.terminateSimulation) {
        fmi2Flag = fmu->enterContinuousTimeMode(state->component);
        if (fmi2Flag > fmi2Warning) {
            fmu->freeInstance(state->component);
            free(state);
            return NULL;
        }
    }

    // Initialize variables and output array
	// Output is an array which value get replaced with each itearation
    get_variable_list(&state->variables);
    state->nVariables = get_variable_count();
    state->output = (double*)calloc(state->nVariables + 1, sizeof(double*));

    // Initialize first output values
    for (int i = 0; i < state->nVariables - 1; i++) {
        if (state->variables[i].type == REAL) {
            fmu->getReal(state->component, &state->variables[i].valueReference, 
                        1, &state->output[i+1]);
        } else if (state->variables[i].type == INTEGER) {
            fmi2Integer intValue;
            fmu->getInteger(state->component, &state->variables[i].valueReference, 
                           1, &intValue);
            state->output[i + 1] = (double)intValue;
        }
    }

    return state;
}

/**
 * @brief Performs one simulation step and updates the simulation state.
 *
 * @param fmu Pointer to the FMU structure
 * @param state Pointer to the simulation state
 * @return fmi2Status Status of the simulation step
 */
fmi2Status simulationDoStep(FMU *fmu, SimulationState *state) {
    INFO("Entering simulation loop\n");
	if (state->time >= state->tEnd || state->eventInfo.terminateSimulation) {
        INFO("Simulation already terminated\n");
		return fmi2Discard;
    }

    fmi2Status fmi2Flag;
    double tPre = state->time;
    double dt;
    fmi2Boolean timeEvent, stateEvent, stepEvent, terminateSimulation;

    // Get current state and derivatives
    fmi2Flag = fmu->getContinuousStates(state->component, state->x, state->nx);
    if (fmi2Flag > fmi2Warning) return fmi2Flag;

    fmi2Flag = fmu->getDerivatives(state->component, state->xdot, state->nx);
    if (fmi2Flag > fmi2Warning) return fmi2Flag;

	INFO("States and derivatives retrieved\n");

    // Advance time
    state->time = min(state->time + state->h, state->tEnd);
    timeEvent = state->eventInfo.nextEventTimeDefined && 
                state->time >= state->eventInfo.nextEventTime;
    
    if (timeEvent) state->time = state->eventInfo.nextEventTime;
    dt = state->time - tPre;
    
    fmi2Flag = fmu->setTime(state->component, state->time);
    if (fmi2Flag > fmi2Warning) return fmi2Flag;

	INFO("Time set\n");

    // Perform one step (forward Euler)
    for (int i = 0; i < state->nx; i++) {
        state->x[i] += dt * state->xdot[i];
    }
    
    fmi2Flag = fmu->setContinuousStates(state->component, state->x, state->nx);
    if (fmi2Flag > fmi2Warning) return fmi2Flag;

	INFO("Step performed\n");

    // Check for state event
    for (int i = 0; i < state->nz; i++) {
        state->prez[i] = state->z[i];
    }
    
    fmi2Flag = fmu->getEventIndicators(state->component, state->z, state->nz);
    if (fmi2Flag > fmi2Warning) return fmi2Flag;

    stateEvent = fmi2False;
    for (int i = 0; i < state->nz; i++) {
        stateEvent = stateEvent || (state->prez[i] * state->z[i] < 0);
    }

	INFO("State event checked\n");

    // Check for step event
    fmi2Flag = fmu->completedIntegratorStep(state->component, fmi2True, 
                                           &stepEvent, &terminateSimulation);
    if (fmi2Flag > fmi2Warning) return fmi2Flag;

    if (terminateSimulation) {
        state->eventInfo.terminateSimulation = fmi2True;
        return fmi2OK;
    }

	INFO("Step event checked\n");

    // Handle events
    if (timeEvent || stateEvent || stepEvent) {
        fmi2Flag = fmu->enterEventMode(state->component);
        if (fmi2Flag > fmi2Warning) return fmi2Flag;

        if (timeEvent) state->nTimeEvents++;
        if (stateEvent) state->nStateEvents++;
        if (stepEvent) state->nStepEvents++;
		INFO("Event handled\n");

        // Event iteration
        state->eventInfo.newDiscreteStatesNeeded = fmi2True;
        state->eventInfo.terminateSimulation = fmi2False;
        
        while (state->eventInfo.newDiscreteStatesNeeded && 
               !state->eventInfo.terminateSimulation) {
            fmi2Flag = fmu->newDiscreteStates(state->component, &state->eventInfo);
            if (fmi2Flag > fmi2Warning) return fmi2Flag;
        }

        if (state->eventInfo.terminateSimulation) {
            return fmi2OK;
        }

        // Re-enter continuous-time mode
        fmi2Flag = fmu->enterContinuousTimeMode(state->component);
        if (fmi2Flag > fmi2Warning) return fmi2Flag;
    }

    // Update outputs
    for (int i = 0; i < state->nVariables - 1; i++) {
        if (state->variables[i].type == REAL) {
            fmu->getReal(state->component, &state->variables[i].valueReference, 
                        1, &state->output[i + 1]);
			INFO("DEBUG:   %s (ref %d): %f\n", state->variables[i].name, 
                   state->variables[i].valueReference, state->output[i + 1]);
        } else if (state->variables[i].type == INTEGER) {
            fmi2Integer intValue;
            fmu->getInteger(state->component, &state->variables[i].valueReference, 
                           1, &intValue);
            state->output[i + 1] = (double)intValue;
        }
    }

    state->nSteps++;
    return fmi2OK;
}






// Test for yield statement

// Structure pour stocker l'état du générateur
typedef struct example_My_Generator_obj_t {
	mp_obj_base_t base;
	SimulationState state;
} example_My_Generator_obj_t;

// Fonction pour avoir le nombre current sans l'incrémenter
// static mp_obj_t example_MyGenerator_getiter(mp_obj_t self_in) {
// 	example_My_Generator_obj_t *self = MP_OBJ_TO_PTR(self_in);
// 	return mp_obj_new_int(self->current);
// }

// On l'associe a une fonction python
//static MP_DEFINE_CONST_FUN_OBJ_1(example_MyGenerator_getCurrent_obj, example_MyGenerator_getiter);

// Fonction print, gère MyGenerator.__repr__ et MyGenerator.__str__
static void example_MyGenerator_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
	example_My_Generator_obj_t *self = MP_OBJ_TO_PTR(self_in);
	if (kind == PRINT_STR) {
		mp_printf(print, "MyGenerator(%d, %d)", self->state.nSteps*self->state.h, self->state.tEnd);
	} else {
		mp_printf(print, "%d", self->state.nSteps);
	}
}



// Fonction "itérable" appelée pour obtenir le prochain élément
static mp_obj_t my_generator_next(mp_obj_t self_in) {
	example_My_Generator_obj_t *self = MP_OBJ_TO_PTR(self_in);

	simulationDoStep(&fmu, &self->state);


	if (self->state.time >= self->state.tEnd || self->state.eventInfo.terminateSimulation) {
		return mp_make_stop_iteration(MP_OBJ_NULL); // Signal de fin
	}

	mp_obj_t *items = m_new(mp_obj_t, self->state.nVariables); //TODO: Replace output directly by this variable
	items[0] = mp_obj_new_float(self->state.nSteps);
	for (int i = 1; i < self->state.nVariables; i++) {
		items[i] = mp_obj_new_float(self->state.output[i]);
	}
	mp_obj_t value = mp_obj_new_tuple(self->state.nVariables, items);
	return value;
} 

// Fonction initialisation du générateur
// static mp_obj_t my_generator_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
// 	mp_arg_check_num(n_args, n_kw, 1, 1, false); //1 arguments : simulation state
// 	example_My_Generator_obj_t *self;
// 	self = mp_obj_malloc(example_My_Generator_obj_t, type);
// 	self->base.type = type;
// 	self->current = mp_obj_get_int(args[0]);
// 	self->end = mp_obj_get_int(args[1]);
// 	return MP_OBJ_FROM_PTR(self);
// }

// This collects all methods and other static class attributes of the Timer.
// The table structure is similar to the module table, as detailed below.
// static const mp_rom_map_elem_t example_MyGenerator_locals_dict_table[] = {
//      { MP_ROM_QSTR(MP_QSTR_getCurrent), MP_ROM_PTR(&example_MyGenerator_getCurrent_obj) },
// };
//  static MP_DEFINE_CONST_DICT(example_MyGenerator_locals_dict, example_MyGenerator_locals_dict_table);


// Définition du type
MP_DEFINE_CONST_OBJ_TYPE(
	example_type_MyGenerator,
	MP_QSTR_MyGenerator,
	MP_TYPE_FLAG_ITER_IS_ITERNEXT,
	print, example_MyGenerator_print,
	//make_new, my_generator_make_new,
	iter, my_generator_next//,
	//locals_dict,&example_MyGenerator_locals_dict
	);


/**
 * @brief Wrapper function for MicroPython to simulate the FMU model.
 *
 * This function serves as a MicroPython interface to the `simulate` function,
 * allowing users to run simulations from within a MicroPython environment.
 *
 * @param end_time The end time of the simulation as a MicroPython object.
 * @param step_size The step size for the simulation as a MicroPython object.
 * @return A MicroPython integer object indicating the success of the simulation (always returns 1) because it crash in case of failure.
 *
 * The function performs the following steps:
 * 1. Converts the MicroPython objects `end_time` and `step_size` to double values.
 * 2. Initializes logging and category settings.
 * 3. Loads the FMU functions.
 * 4. Calls the `simulate` function with the provided parameters.
 * 5. Returns a MicroPython integer object indicating success.
 */
static mp_obj_t example_simulate(size_t n_args, const mp_obj_t *args) {
	//double tStart = mp_obj_get_float(start_time);
	//printf("tStart: %f\n", tStart);
	double tStart = mp_obj_get_float(args[0]);
	double tEnd = mp_obj_get_float(args[1]);
	double h = mp_obj_get_float(args[2]);
	bool simulateAllBool = mp_obj_is_true(args[3]);




	loadFunctions(&fmu);

	if (simulateAllBool)
	{
		simulate(&fmu, tStart, tEnd, h);
		return mp_obj_new_int(1);
	} else {

		SimulationState *state;
		state = initializeSimulation(&fmu, tStart, tEnd, h);

		example_My_Generator_obj_t *self;
		self = mp_obj_malloc(example_My_Generator_obj_t, &example_type_MyGenerator);
		self->base.type = &example_type_MyGenerator;
		self->state = *state;
		return MP_OBJ_FROM_PTR(self);
	}
}

// On permet l'appel de cette fonction dans python :
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(example_simulate_obj, 4, 4, example_simulate);

// On va mapper les noms des variables et des class :
static const mp_rom_map_elem_t example_module_globals_table[] = {
	{ MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_testlibrary)},
	{ MP_ROM_QSTR(MP_QSTR_simulate), MP_ROM_PTR(&example_simulate_obj)},
	{ MP_ROM_QSTR(MP_QSTR_MyGenerator), MP_ROM_PTR(&example_type_MyGenerator) },
};
static MP_DEFINE_CONST_DICT(example_module_globals, example_module_globals_table);

//On définit le module :
const mp_obj_module_t example_user_testlibrary = {
	.base = { &mp_type_module },
	.globals= (mp_obj_dict_t *)&example_module_globals
};

// Enregistrement le module pour le rendre accessible sous python :
MP_REGISTER_MODULE(MP_QSTR_testlibrary, example_user_testlibrary);
