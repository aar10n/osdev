//
// Created by Aaron Gill-Braun on 2020-09-02.
//

#ifndef LIB_MACROS_REPEAT_H
#define LIB_MACROS_REPEAT_H

#define __REPEAT_0(X)
#define __REPEAT_1(X)X
#define __REPEAT_2(X) __REPEAT_1(X)X
#define __REPEAT_3(X) __REPEAT_2(X)X
#define __REPEAT_4(X) __REPEAT_3(X)X
#define __REPEAT_5(X) __REPEAT_4(X)X
#define __REPEAT_6(X) __REPEAT_5(X)X
#define __REPEAT_7(X) __REPEAT_6(X)X
#define __REPEAT_8(X) __REPEAT_7(X)X
#define __REPEAT_9(X) __REPEAT_8(X)X
#define __REPEAT_10(X) __REPEAT_9(X)X

#define REPEAT(HUNDREDS,TENS,ONES,X) \
  __REPEAT_##HUNDREDS(__REPEAT_10(__REPEAT_10(X))) \
  __REPEAT_##TENS(__REPEAT_10(X)) \
  __REPEAT_##ONES(X)

#endif
