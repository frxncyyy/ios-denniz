#pragma once
extern int *__error(void);
#define errno (*__error())
