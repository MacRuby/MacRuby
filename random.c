/**********************************************************************

  random.c -

  $Author: matz $
  created at: Fri Dec 24 16:39:21 JST 1993

  Copyright (C) 1993-2007 Yukihiro Matsumoto

**********************************************************************/

#include "ruby/ruby.h"

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <limits.h>
#include <math.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif

#include "ruby/node.h"
#include "vm.h"


static void
init_random(void)
{
    srandomdev();
}

static VALUE
random_number_with_limit(long limit)
{
    long	val;

    if (limit == 0) {
	/*
	 * #rand(0) needs to generate a float x, such as 0.0 <= x <= 1.0. However,
	 * srandom() returns a long. We want to convert that long by divide by RAND_MAX
	 */
	val = random();
	return DOUBLE2NUM((double)val / ((unsigned long)RAND_MAX+1));
    }
    else {
	if (limit < 0) {
	    /* -LONG_MIN = LONG_MIN, let's avoid that */
	    if (limit == LONG_MIN) {
		limit += 1;
	    }
	    limit = -limit;
	}
	val = random() % limit;
	return LONG2NUM(val);
    }
    return Qnil;
}

unsigned long rb_genrand_int32(void);

static VALUE
random_number_with_bignum_limit(struct RBignum *bignum_limit)
{
    long	   nb_long_in_bignum;
    long           nb_loops;
    struct RBignum *div_result;
    struct RBignum *result;
    int		   idx;

    div_result = RBIGNUM(rb_big_div((VALUE)bignum_limit, LONG2NUM(LONG_MAX)));
    // FIXME: Shouldn't use !, what value should I check?
    if (!FIXNUM_P((VALUE)div_result)) {
	rb_raise(rb_eArgError, "max is too huge");
    }

    nb_long_in_bignum = FIX2LONG((VALUE)div_result);
    nb_loops = 1 + FIX2LONG(random_number_with_limit(nb_long_in_bignum));
    result = RBIGNUM(rb_int2big(0));
    for (idx = 0; idx < nb_loops; idx++) {
	// This creates a bignum on each iteration... Not really good :-/
	result = RBIGNUM(rb_big_plus((VALUE)result, random_number_with_limit(LONG_MAX)));
    }
    return ((VALUE)result);
}

unsigned long
rb_genrand_int32(void)
{
    unsigned long result;
    short	  nb_loops;
    int		  idx;

    result = 0;
    nb_loops = 1 + (random() % 2);
    for (idx = 0; idx < nb_loops; idx++) {
	result += random();
    }
    return result;
}

double
rb_genrand_real(void)
{
    return NUM2DBL(random_number_with_limit(0));
}

/*
 *  call-seq:
 *     srand(number=0)    => old_seed
 *  
 *  Seeds the pseudorandom number generator to the value of
 *  <i>number</i>.<code>to_i.abs</code>. If <i>number</i> is omitted
 *  or zero, seeds the generator using a combination of the time, the
 *  process id, and a sequence number. (This is also the behavior if
 *  <code>Kernel::rand</code> is called without previously calling
 *  <code>srand</code>, but without the sequence.) By setting the seed
 *  to a known value, scripts can be made deterministic during testing.
 *  The previous seed value is returned. Also see <code>Kernel::rand</code>.
 */

static VALUE
rb_f_srand(VALUE obj, SEL sel, int argc, VALUE *argv)
{
    VALUE	old_seed;
    VALUE       seed_param;
    VALUE	seed_int;
    unsigned	seed = 0;

    if (argc == 0) {
	srandomdev();
	seed = (unsigned)random();
	seed_int = UINT2NUM(seed);
    }
    else {
	rb_scan_args(argc, argv, "01", &seed_param);
	seed_int = rb_to_int(seed_param);
	switch (TYPE(seed_int)) {
	  case T_BIGNUM:
	    // In case we need to keep 'seed.to_i.abs'
	    /*
	    if (RBIGNUM_NEGATIVE_P(seed_int)) {
		seed_int = rb_big_uminus(seed_int);
	    }
	    */
	    for (int i = 0; i < RBIGNUM_LEN(seed_int); i++) {
		seed += (unsigned int)RBIGNUM_DIGITS(seed_int)[i];
	    }
	    break ;
	  case T_FIXNUM:
	    // In case we need to keep 'seed.to_i.abs'
	    /*
	    if (FIX2LONG(seed_int) < 0) {
		seed_int = rb_fix_uminus(seed_int);
	    }
	    */
	    seed = (unsigned int)FIX2LONG(seed_int);
	    break ;
	}
    }
    srandom(seed);

    old_seed = rb_vm_rand_seed();
    // Ruby's behaviour is weird. It stores the 'seed.to_i' value, instead of
    // the 'seed.to_i.abs' value, or just 'seed'. Which one should we use?
    rb_vm_set_rand_seed(seed_int);
    return old_seed;
}

/*
 *  call-seq:
 *     rand(max=0)    => number
 *  
 *  Converts <i>max</i> to an integer using max1 =
 *  max<code>.to_i.abs</code>. If the result is zero, returns a
 *  pseudorandom floating point number greater than or equal to 0.0 and
 *  less than 1.0. Otherwise, returns a pseudorandom integer greater
 *  than or equal to zero and less than max1. <code>Kernel::srand</code>
 *  may be used to ensure repeatable sequences of random numbers between
 *  different runs of the program. Ruby currently uses a modified
 *  Mersenne Twister with a period of 2**19937-1.
 *     
 *     srand 1234                 #=> 0
 *     [ rand,  rand ]            #=> [0.191519450163469, 0.49766366626136]
 *     [ rand(10), rand(1000) ]   #=> [6, 817]
 *     srand 1234                 #=> 1234
 *     [ rand,  rand ]            #=> [0.191519450163469, 0.49766366626136]
 */

static VALUE
rb_f_rand(VALUE obj, SEL sel, int argc, VALUE *argv)
{
    VALUE	arg_max;
    long	max;

    rb_scan_args(argc, argv, "01", &arg_max);
    switch (TYPE(arg_max)) {
      case T_FLOAT:
	if (RFLOAT_VALUE(arg_max) <= LONG_MAX && RFLOAT_VALUE(arg_max) >= LONG_MIN) {
	    max = (long)RFLOAT_VALUE(arg_max);
	    break;
	}
        if (RFLOAT_VALUE(arg_max) < 0)
            arg_max = rb_dbl2big(-RFLOAT_VALUE(arg_max));
        else
            arg_max = rb_dbl2big(RFLOAT_VALUE(arg_max));
	/* fall through */
      case T_BIGNUM:
      bignum:
        {
            struct RBignum *limit = (struct RBignum *)arg_max;
            if (!RBIGNUM_SIGN(limit)) {
                limit = (struct RBignum *)rb_big_clone(arg_max);
                RBIGNUM_SET_SIGN(limit, 1);
            }
            limit = (struct RBignum *)rb_big_minus((VALUE)limit, INT2FIX(1));
            if (FIXNUM_P((VALUE)limit)) {
		max = FIX2LONG((VALUE)limit) + 1;
		break;
            }
            return random_number_with_bignum_limit(limit);
	}
      case T_NIL:
	max = 0;
	break;
      default:
	arg_max = rb_Integer(arg_max);
	if (TYPE(arg_max) == T_BIGNUM) goto bignum;
      case T_FIXNUM:
	max = FIX2LONG(arg_max);
	break;
    }

    return random_number_with_limit(max);
}

void
Init_Random(void)
{
    init_random();
    rb_objc_define_module_function(rb_mKernel, "srand", rb_f_srand, -1);
    rb_objc_define_module_function(rb_mKernel, "rand", rb_f_rand, -1);
}
