#ifndef MOUSE_H
#define MOUSE_H

extern int mouse_x;
extern int mouse_y;
extern int mouse_left_button;

void mouse_init(void);
int  mouse_poll(void);

#endif