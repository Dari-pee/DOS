#ifndef PIC_H
#define PIC_H

void pic_init(void);
void pic_unmask(int irq);
void pic_eoi(int irq);

#endif
