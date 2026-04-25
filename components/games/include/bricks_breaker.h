#ifndef BRICKS_BREAKER_H
#define BRICKS_BREAKER_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

void bricks_breaker_init(lv_obj_t *parent);
void bricks_breaker_start(void);
void bricks_breaker_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* BRICKS_BREAKER_H */