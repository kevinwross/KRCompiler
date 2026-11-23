#ifndef KR232_INPUT_H
#define KR232_INPUT_H

/* The keyboard's event queue (kr232-devices.md section 6). One event is a packed
   word; NO_EVENT means the queue is empty. */
#define KR232_NO_EVENT 0xFFFFFFFFu

unsigned input_next(void);
unsigned input_pending(void);

#endif
