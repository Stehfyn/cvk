#ifndef IMMERSIVE_OPUS3_NT_ASSERT_H
#define IMMERSIVE_OPUS3_NT_ASSERT_H

#define ASSERT(Expression) ((Expression) ? (void)0 : DbgRaiseAssertionFailure())

#endif
