# MicroFMU

#### Problème :
On doit modifier le fichier fmi2Functions.c.

La ligne :
```c
S->stopTime = stopTimeDefined ? stopTime : INFINITY;
```

Doit devenir
```c
S->stopTime = stopTimeDefined ? (fmi2Real)stopTime : (fmi2Real)INFINITY;
```