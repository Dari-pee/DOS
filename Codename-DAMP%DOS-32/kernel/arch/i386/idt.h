#ifndef IDT_H
#define IDT_H

void idt_install(void);
void idtsh(int vector, void *handler);

#endif