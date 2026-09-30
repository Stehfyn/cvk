#pragma once

#define ASSERT(Expression) ((Expression) ? (void)0 : DbgRaiseAssertionFailure())
