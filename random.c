/*
 * Random Numbers.
 *
 * This file is covered by the Ruby license. See COPYING for more details.
 *
 * Copyright (C) 2012, The MacRuby Team. All rights reserved.
 * Copyright (C) 2007-2011, Apple Inc. All rights reserved.
 * Copyright (C) 1993-2007 Yukihiro Matsumoto
 */

#include "macruby_internal.h"
#include "ruby/node.h"
#include "vm.h"
#include "id.h"
#include "encoding.h"

#include <unistd.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <math.h>
#include <errno.h>

#include "mt.c"

VALUE rb_cRandom;

typedef struct {
    VALUE seed;
    struct MT mt;
} rb_random_t;

#define get_rnd(obj) ((rb_random_t *)DATA_PTR(obj))
#define default_rnd() (get_rnd(rb_vm_default_random()))

unsigned int
rb_genrand_int32(void)
{
    return genrand_int32(&default_rnd()->mt);
}

double
rb_genrand_real(void)
{
    return genrand_real(&default_rnd()->mt);
}

#define BDIGITS(x) (RBIGNUM_DIGITS(x))
#define BITSPERDIG (SIZEOF_BDIGITS*CHAR_BIT)
#define BIGRAD ((BDIGIT_DBL)1 << BITSPERDIG)
#define DIGSPERINT (SIZEOF_INT/SIZEOF_BDIGITS)
#define BIGUP(x) ((BDIGIT_DBL)(x) << BITSPERDIG)
#define BIGDN(x) RSHIFT(x,BITSPERDIG)
#define BIGLO(x) ((BDIGIT)((x) & (BIGRAD-1)))
#define BDIGMAX ((BDIGIT)-1)

#define roomof(n, m) (int)(((n)+(m)-1) / (m))
#define numberof(array) (int)(sizeof(array) / sizeof((array)[0]))
#define SIZEOF_INT32 (31/CHAR_BIT + 1)

static VALUE random_seed(VALUE, SEL);

/* :nodoc: */
static VALUE
random_alloc(VALUE klass, SEL sel)
{
    rb_random_t *r = (rb_random_t *)xmalloc(sizeof(rb_random_t));
    r->seed = INT2FIX(0);
    return Data_Wrap_Struct(rb_cRandom, NULL, NULL, r);
}

static VALUE
rand_init(struct MT *mt, VALUE vseed)
{
    VALUE seed;
    long blen = 0;
    long fixnum_seed;
    int i, j, len;
    unsigned int buf0[SIZEOF_LONG / SIZEOF_INT32 * 4], *buf = buf0;

    seed = rb_to_int(vseed);
    switch (TYPE(seed)) {
      case T_FIXNUM:
	len = 1;
	fixnum_seed = FIX2LONG(seed);
        if (fixnum_seed < 0)
            fixnum_seed = -fixnum_seed;
	buf[0] = (unsigned int)(fixnum_seed & 0xffffffff);
#if SIZEOF_LONG > SIZEOF_INT32
	if ((long)(int)fixnum_seed != fixnum_seed) {
	    if ((buf[1] = (unsigned int)(fixnum_seed >> 32)) != 0) ++len;
	}
#endif
	break;
      case T_BIGNUM:
	blen = RBIGNUM_LEN(seed);
	if (blen == 0) {
	    len = 1;
	}
	else {
	    if (blen > MT_MAX_STATE * SIZEOF_INT32 / SIZEOF_BDIGITS)
		blen = (len = MT_MAX_STATE) * SIZEOF_INT32 / SIZEOF_BDIGITS;
	    len = roomof((int)blen * SIZEOF_BDIGITS, SIZEOF_INT32);
	}
	/* allocate ints for init_by_array */
	if (len > numberof(buf0)) buf = ALLOC_N(unsigned int, len);
	memset(buf, 0, len * sizeof(*buf));
	len = 0;
	for (i = (int)(blen-1); 0 <= i; i--) {
	    j = i * SIZEOF_BDIGITS / SIZEOF_INT32;
#if SIZEOF_BDIGITS < SIZEOF_INT32
	    buf[j] <<= BITSPERDIG;
#endif
	    buf[j] |= RBIGNUM_DIGITS(seed)[i];
	    if (!len && buf[j]) len = j;
	}
	++len;
	break;
      default:
	rb_raise(rb_eTypeError, "failed to convert %s into Integer",
		 rb_obj_classname(vseed));
    }
    if (len <= 1) {
        init_genrand(mt, buf[0]);
    }
    else {
        if (buf[len-1] == 1) /* remove leading-zero-guard */
            len--;
        init_by_array(mt, buf, len);
    }
    return seed;
}

/*
 * call-seq: Random.new([seed]) -> prng
 *
 * Creates new Mersenne Twister based pseudorandom number generator with
 * seed.  When the argument seed is omitted, the generator is initialized
 * with Random.seed.
 *
 * The argument seed is used to ensure repeatable sequences of random numbers
 * between different runs of the program.
 *
 *     prng = Random.new(1234)
 *     [ prng.rand, prng.rand ]   #=> [0.191519450378892, 0.622108771039832]
 *     [ prng.integer(10), prng.integer(1000) ]  #=> [4, 664]
 *     prng = Random.new(1234)
 *     [ prng.rand, prng.rand ]   #=> [0.191519450378892, 0.622108771039832]
 */
static VALUE
random_init(VALUE obj, SEL sel, int argc, VALUE *argv)
{
    VALUE vseed;
    rb_random_t *rnd = get_rnd(obj);

    if (argc == 0) {
	vseed = random_seed(0, 0);
    }
    else {
	rb_scan_args(argc, argv, "01", &vseed);
    }
    GC_WB(&rnd->seed, rand_init(&rnd->mt, vseed));
    return obj;
}

#define DEFAULT_SEED_CNT 4
#define DEFAULT_SEED_LEN (DEFAULT_SEED_CNT * sizeof(int))

static void
fill_random_seed(unsigned int seed[DEFAULT_SEED_CNT])
{
    static int n = 0;
    struct timeval tv;
    int fd;
    struct stat statbuf;

    memset(seed, 0, DEFAULT_SEED_LEN);

    if ((fd = open("/dev/urandom", O_RDONLY
#ifdef O_NONBLOCK
            |O_NONBLOCK
#endif
#ifdef O_NOCTTY
            |O_NOCTTY
#endif
#ifdef O_NOFOLLOW
            |O_NOFOLLOW
#endif
            )) >= 0) {
        if (fstat(fd, &statbuf) == 0 && S_ISCHR(statbuf.st_mode)) {
            (void)read(fd, seed, DEFAULT_SEED_LEN);
        }
        close(fd);
    }

    gettimeofday(&tv, 0);
    seed[0] ^= tv.tv_usec;
    seed[1] ^= (unsigned int)tv.tv_sec;
#if SIZEOF_TIME_T > SIZEOF_INT
    seed[0] ^= (unsigned int)((time_t)tv.tv_sec >> SIZEOF_INT * CHAR_BIT);
#endif
    seed[2] ^= getpid() ^ (n++ << 16);
    seed[3] ^= (unsigned int)(VALUE)&seed;
#if SIZEOF_VOIDP > SIZEOF_INT
    seed[2] ^= (unsigned int)((VALUE)&seed >> SIZEOF_INT * CHAR_BIT);
#endif
}

static VALUE
make_seed_value(const void *ptr)
{
    const long len = DEFAULT_SEED_LEN/SIZEOF_BDIGITS;
    BDIGIT *digits;
    NEWOBJ(big, struct RBignum);
    OBJSETUP(big, rb_cBignum, T_BIGNUM);

    RBIGNUM_SET_SIGN(big, 1);
    rb_big_resize((VALUE)big, len + 1);
    digits = RBIGNUM_DIGITS(big);

    MEMCPY(digits, ptr, char, DEFAULT_SEED_LEN);

    /* set leading-zero-guard if need. */
    digits[len] =
#if SIZEOF_INT32 / SIZEOF_BDIGITS > 1
	digits[len-2] <= 1 && digits[len-1] == 0
#else
	digits[len-1] <= 1
#endif
	? 1 : 0;

    return rb_big_norm((VALUE)big);
}

/*
 * call-seq: Random.seed -> integer
 *
 * Returns arbitrary value for seed.
 */
static VALUE
random_seed(VALUE rcv, SEL sel)
{
    unsigned int buf[DEFAULT_SEED_CNT];
    fill_random_seed(buf);
    return make_seed_value(buf);
}

/*
 * call-seq: prng.seed -> integer
 *
 * Returns the seed of the generator.
 */
static VALUE
random_get_seed(VALUE obj, SEL sel)
{
    return get_rnd(obj)->seed;
}

/* :nodoc: */
static VALUE
random_copy(VALUE obj, SEL sel, VALUE orig)
{
    rb_random_t *rnd1 = get_rnd(obj);
    rb_random_t *rnd2 = get_rnd(orig);
    struct MT *mt = &rnd1->mt;

    *rnd1 = *rnd2;
    GC_WB(&rnd1->seed, rnd2->seed);
    mt->next = mt->state + numberof(mt->state) - mt->left + 1;
    return obj;
}

static VALUE
mt_state(const struct MT *mt)
{
    const int n = numberof(mt->state);
    VALUE bigo = rb_big_new(n, 1);
    BDIGIT *d = RBIGNUM_DIGITS(bigo);
    for (int i = 0; i < n; i++) {
	unsigned int x = mt->state[i];
	*d++ = (BDIGIT)x;
    }
    return rb_big_norm(bigo);
}

/* :nodoc: */
static VALUE
random_state(VALUE obj, SEL sel)
{
    rb_random_t *rnd = get_rnd(obj);
    return mt_state(&rnd->mt);
}

/* :nodoc: */
static VALUE
random_s_state(VALUE klass, SEL sel)
{
    return mt_state(&default_rnd()->mt);
}

/* :nodoc: */
static VALUE
random_left(VALUE obj, SEL sel)
{
    rb_random_t *rnd = get_rnd(obj);
    return INT2FIX(rnd->mt.left);
}

/* :nodoc: */
static VALUE
random_s_left(VALUE klass, SEL sel)
{
    return INT2FIX(default_rnd()->mt.left);
}

/* :nodoc: */
static VALUE
random_dump(VALUE obj, SEL sel)
{
    rb_random_t *rnd = get_rnd(obj);
    VALUE dump = rb_ary_new2(3);

    rb_ary_push(dump, mt_state(&rnd->mt));
    rb_ary_push(dump, INT2FIX(rnd->mt.left));
    rb_ary_push(dump, rnd->seed);

    return dump;
}

/* :nodoc: */
static VALUE
random_load(VALUE obj, SEL sel, VALUE dump)
{
    rb_random_t *rnd = get_rnd(obj);
    struct MT *mt = &rnd->mt;
    VALUE state, left = INT2FIX(1), seed = INT2FIX(0);
    unsigned long x;

    Check_Type(dump, T_ARRAY);
    switch (RARRAY_LEN(dump)) {
      case 3:
	seed = RARRAY_AT(dump, 2);
      case 2:
	left = RARRAY_AT(dump, 1);
      case 1:
	state = RARRAY_AT(dump, 0);
	break;
      default:
	rb_raise(rb_eArgError, "wrong dump data");
    }
    memset(mt->state, 0, sizeof(mt->state));
#if 0
    if (FIXNUM_P(state)) {
	x = FIX2ULONG(state);
	mt->state[0] = (unsigned int)x;
#if SIZEOF_LONG / SIZEOF_INT >= 2
	mt->state[1] = (unsigned int)(x >> BITSPERDIG);
#endif
#if SIZEOF_LONG / SIZEOF_INT >= 3
	mt->state[2] = (unsigned int)(x >> 2 * BITSPERDIG);
#endif
#if SIZEOF_LONG / SIZEOF_INT >= 4
	mt->state[3] = (unsigned int)(x >> 3 * BITSPERDIG);
#endif
    }
    else {
	BDIGIT *d;
	long len;
	Check_Type(state, T_BIGNUM);
	len = RBIGNUM_LEN(state);
	if (len > roomof(sizeof(mt->state), SIZEOF_BDIGITS)) {
	    len = roomof(sizeof(mt->state), SIZEOF_BDIGITS);
	}
#if SIZEOF_BDIGITS < SIZEOF_INT
	else if (len % DIGSPERINT) {
	    d = RBIGNUM_DIGITS(state) + len;
# if DIGSPERINT == 2
	    --len;
	    x = *--d;
# else
	    x = 0;
	    do {
		x = (x << BITSPERDIG) | *--d;
	    } while (--len % DIGSPERINT);
# endif
	    mt->state[len / DIGSPERINT] = (unsigned int)x;
	}
#endif
	if (len > 0) {
	    d = BDIGITS(state) + len;
	    do {
		--len;
		x = *--d;
# if DIGSPERINT == 2
		--len;
		x = (x << BITSPERDIG) | *--d;
# elif SIZEOF_BDIGITS < SIZEOF_INT
		do {
		    x = (x << BITSPERDIG) | *--d;
		} while (--len % DIGSPERINT);
# endif
		mt->state[len / DIGSPERINT] = (unsigned int)x;
	    } while (len > 0);
	}
    }
#endif
    x = NUM2ULONG(left);
    if (x > numberof(mt->state)) {
	rb_raise(rb_eArgError, "wrong value");
    }
    mt->left = (unsigned int)x;
    mt->next = mt->state + numberof(mt->state) - x + 1;
    GC_WB(&rnd->seed, rb_to_int(seed));

    return obj;
}

/*
 *  call-seq:
 *     srand(number=0)    => old_seed
 *
 *  Seeds the pseudorandom number generator to the value of
 *  <i>number</i>. If <i>number</i> is omitted
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
    VALUE seed, old;
    rb_random_t *r = default_rnd();

    rb_secure(4);
    if (argc == 0) {
	seed = random_seed(0, 0);
    }
    else {
	rb_scan_args(argc, argv, "01", &seed);
    }
    old = r->seed;
    GC_WB(&r->seed, rand_init(&r->mt, seed));

    return old;
}

static unsigned long
make_mask(unsigned long x)
{
    x = x | x >> 1;
    x = x | x >> 2;
    x = x | x >> 4;
    x = x | x >> 8;
    x = x | x >> 16;
#if 4 < SIZEOF_LONG
    x = x | x >> 32;
#endif
    return x;
}

static unsigned long
limited_rand(struct MT *mt, unsigned long limit)
{
    if (limit == 0) {
	return 0;
    }
    unsigned long val, mask = make_mask(limit);
retry:
    val = 0;
    for (int i = SIZEOF_LONG / SIZEOF_INT32 - 1; 0 <= i; i--) {
        if ((mask >> (i * 32)) & 0xffffffff) {
            val |= (unsigned long)genrand_int32(mt) << (i * 32);
            val &= mask;
            if (limit < val) {
                goto retry;
	    }
        }
    }
    return val;
}

static VALUE
limited_big_rand(struct MT *mt, struct RBignum *limit)
{
#if SIZEOF_BDIGITS < 8
    const long len = (RBIGNUM_LEN(limit) * SIZEOF_BDIGITS + 3) / 4;
#else
    const long len = (RBIGNUM_LEN(limit) * SIZEOF_BDIGITS + 3) / 8;
#endif
    struct RBignum *val = (struct RBignum *)rb_big_clone((VALUE)limit);
    RBIGNUM_SET_SIGN(val, 1);
#if SIZEOF_BDIGITS == 2
# define BIG_GET32(big,i) \
    (RBIGNUM_DIGITS(big)[(i)*2] | \
     ((i)*2+1 < RBIGNUM_LEN(big) ? \
      (RBIGNUM_DIGITS(big)[(i)*2+1] << 16) : \
      0))
# define BIG_SET32(big,i,d) \
    ((RBIGNUM_DIGITS(big)[(i)*2] = (d) & 0xffff), \
     ((i)*2+1 < RBIGNUM_LEN(big) ? \
      (RBIGNUM_DIGITS(big)[(i)*2+1] = (d) >> 16) : \
      0))
#else
    /* SIZEOF_BDIGITS == 4 */
# define BIG_GET32(big,i) (RBIGNUM_DIGITS(big)[i])
# define BIG_SET32(big,i,d) (RBIGNUM_DIGITS(big)[i] = (d))
#endif
    unsigned long mask;
    int boundary;
retry:
    mask = 0;
    boundary = 1;
    for (long i = len - 1; 0 <= i; i--) {
        const unsigned long lim = BIG_GET32(limit, i);
	unsigned long rnd;
        mask = mask != 0 ? 0xffffffff : make_mask(lim);
        if (mask != 0) {
            rnd = genrand_int32(mt) & mask;
            if (boundary) {
                if (lim < rnd) {
                    goto retry;
		}
                if (rnd < lim) {
                    boundary = 0;
		}
            }
        }
        else {
            rnd = 0;
        }
        BIG_SET32(val, i, (BDIGIT)rnd);
    }
    return rb_big_norm((VALUE)val);
}

unsigned long
rb_rand_internal(unsigned long i)
{
    struct MT *mt = &default_rnd()->mt;
    if (!genrand_initialized(mt)) {
	rand_init(mt, random_seed(0, 0));
    }
    return limited_rand(mt, i);
}

unsigned int
rb_random_int32(VALUE obj)
{
    rb_random_t *rnd = get_rnd(obj);
    return genrand_int32(&rnd->mt);
}

double
rb_random_real(VALUE obj)
{
    rb_random_t *rnd = get_rnd(obj);
    return genrand_real(&rnd->mt);
}

/*
 * call-seq: prng.bytes(size) -> prng
 *
 * Returns a random binary string.  The argument size specified the length of
 * the result string.
 */
static VALUE
random_bytes(VALUE obj, SEL sel, VALUE len)
{
    long n = NUM2LONG(rb_to_int(len));
    VALUE bytes = rb_bstr_new();
    if (n <= 0) {
	return bytes;
    } 
    rb_bstr_resize(bytes, n);
    rb_random_t *rnd = get_rnd(obj);
    uint8_t *ptr = rb_bstr_bytes(bytes);
    unsigned int r, i;

    for (; n >= SIZEOF_INT32; n -= SIZEOF_INT32) {
	r = genrand_int32(&rnd->mt);
	i = SIZEOF_INT32;
	do {
	    *ptr++ = (uint8_t)r;
	    r >>= CHAR_BIT;
        } while (--i);
    }
    if (n > 0) {
	r = genrand_int32(&rnd->mt);
	do {
	    *ptr++ = (uint8_t)r;
	    r >>= CHAR_BIT;
	} while (--n);
    }
    return bytes;
}

static VALUE
range_values(VALUE vmax, VALUE *begp, int *exclp)
{
    VALUE end;
    if (!rb_range_values(vmax, begp, &end, exclp)) {
	return Qfalse;
    }
    if (!rb_vm_respond_to(end, selMINUS, false)) {
	return Qfalse;
    }
    VALUE r = rb_vm_call(end, selMINUS, 1, begp);
    if (NIL_P(r)) {
	return Qfalse;
    }
    return r;
}

static VALUE
rand_int(struct MT *mt, VALUE vmax, int restrictive)
{
    long max;
    unsigned long r;

    if (FIXNUM_P(vmax)) {
	max = FIX2LONG(vmax);
	if (!max) return Qnil;
	if (max < 0) {
	    if (restrictive) return Qnil;
	    max = -max;
	}
	r = limited_rand(mt, (unsigned long)max - 1);
	return ULONG2NUM(r);
    }
    else {
	VALUE ret;
	if (rb_bigzero_p(vmax)) return Qnil;
	if (!RBIGNUM_SIGN(vmax)) {
	    if (restrictive) return Qnil;
	    vmax = rb_big_clone(vmax);
	    RBIGNUM_SET_SIGN(vmax, 1);
	}
	vmax = rb_big_minus(vmax, INT2FIX(1));
	if (FIXNUM_P(vmax)) {
	    max = FIX2LONG(vmax);
	    if (max == -1) return Qnil;
	    r = limited_rand(mt, max);
	    return LONG2NUM(r);
	}
	ret = limited_big_rand(mt, RBIGNUM(vmax));
	return ret;
    }
}

static inline double
float_value(VALUE v)
{
    double x = RFLOAT_VALUE(v);
    if (isinf(x) || isnan(x)) {
	VALUE error = INT2FIX(EDOM);
	rb_exc_raise(rb_class_new_instance(1, &error, rb_eSystemCallError));
    }
    return x;
}

/*
 * call-seq:
 *     prng.rand -> float
 *     prng.rand(limit) -> number
 *
 * When the argument is an +Integer+ or a +Bignum+, it returns a
 * random integer greater than or equal to zero and less than the
 * argument.  Unlike Random.rand, when the argument is a negative
 * integer or zero, it raises an ArgumentError.
 *
 * When the argument is a +Float+, it returns a random floating point
 * number between 0.0 and _max_, including 0.0 and excluding _max_.
 *
 * When the argument _limit_ is a +Range+, it returns a random
 * number where range.member?(number) == true.
 *     prng.rand(5..9)  # => one of [5, 6, 7, 8, 9]
 *     prng.rand(5...9) # => one of [5, 6, 7, 8]
 *     prng.rand(5.0..9.0) # => between 5.0 and 9.0, including 9.0
 *     prng.rand(5.0...9.0) # => between 5.0 and 9.0, excluding 9.0
 *
 * +begin+/+end+ of the range have to have subtract and add methods.
 *
 * Otherwise, it raises an ArgumentError.
 */
static VALUE
random_rand(VALUE obj, SEL sel, int argc, VALUE *argv)
{
    rb_random_t *rnd = get_rnd(obj);
    VALUE beg = Qundef, v;
    int excl = 0;

    if (argc == 0) {
	return rb_float_new(genrand_real(&rnd->mt));
    }
    else if (argc != 1) {
	rb_raise(rb_eArgError, "wrong number of arguments (%d for 0..1)", argc);
    }
    VALUE vmax = argv[0];
    if (NIL_P(vmax)) {
	v = Qnil;
    }
    else if (TYPE(vmax) != T_FLOAT
	    && (v = rb_check_to_integer(vmax, "to_int"), !NIL_P(v))) {
	v = rand_int(&rnd->mt, vmax = v, 1);
    }
    else if (v = rb_check_to_float(vmax), !NIL_P(v)) {
	const double max = float_value(v);
	if (max > 0.0) {
	    v = rb_float_new(max * genrand_real(&rnd->mt));
	} 
	else {
	    v = Qnil;
	}
    }
    else if ((v = range_values(vmax, &beg, &excl)) != Qfalse) {
	vmax = v;
	if (TYPE(vmax) != T_FLOAT
		&& (v = rb_check_to_integer(vmax, "to_int"), !NIL_P(v))) {
	    long max;
	    vmax = v;
	    v = Qnil;
	    if (FIXNUM_P(vmax)) {
fixnum:
		if ((max = FIX2LONG(vmax) - excl) >= 0) {
		    unsigned long r = limited_rand(&rnd->mt, (unsigned long)max);
		    v = ULONG2NUM(r);
		}
	    }
	    else if (BUILTIN_TYPE(vmax) == T_BIGNUM && RBIGNUM_SIGN(vmax)
		    && !rb_bigzero_p(vmax)) {
		vmax = excl ? rb_big_minus(vmax, INT2FIX(1)) : rb_big_norm(vmax);
		if (FIXNUM_P(vmax)) {
		    excl = 0;
		    goto fixnum;
		}
		v = limited_big_rand(&rnd->mt, RBIGNUM(vmax));
	    }
	}
	else if (v = rb_check_to_float(vmax), !NIL_P(v)) {
	    double max = float_value(v), r;
	    v = Qnil;
	    if (max > 0.0) {
		if (excl) {
		    r = genrand_real(&rnd->mt);
		}
		else {
		    r = genrand_real2(&rnd->mt);
		}
		v = rb_float_new(r * max);
	    }
	    else if (max == 0.0 && !excl) {
		v = rb_float_new(0.0);
	    }
	}
    }
    else {
	v = Qnil;
	(void)NUM2LONG(vmax);
    }
    if (NIL_P(v)) {
	VALUE mesg = rb_str_new2("invalid argument - ");
	rb_str_append(mesg, rb_obj_as_string(argv[0]));
	rb_exc_raise(rb_exc_new3(rb_eArgError, mesg));
    }
    if (beg == Qundef) {
	return v;
    }
    if (FIXNUM_P(beg) && FIXNUM_P(v)) {
	long x = FIX2LONG(beg) + FIX2LONG(v);
	return LONG2NUM(x);
    }
    switch (TYPE(v)) {
	case T_BIGNUM:
	    return rb_big_plus(v, beg);
	case T_FLOAT:
	    {
		double d = RFLOAT_VALUE(v) + RFLOAT_VALUE(rb_check_to_float(beg));
		return DOUBLE2NUM(d);
	    }
	default:
	    return rb_vm_call(v, selPLUS, 1, &beg);
    }
}

/*
 * call-seq:
 *     prng1 == prng2 -> true or false
 *
 * Returns true if the generators' states equal.
 */
static VALUE
random_equal(VALUE self, SEL sel, VALUE other)
{
    if (rb_obj_class(self) != rb_obj_class(other)) {
	return Qfalse;
    }
    rb_random_t *r1 = get_rnd(self);
    rb_random_t *r2 = get_rnd(other);
    if (rb_equal(r1->seed, r2->seed) != Qtrue) {
	return Qfalse;
    }
    if (memcmp(r1->mt.state, r2->mt.state, sizeof(r1->mt.state))) {
	return Qfalse;
    }
    if ((r1->mt.next - r1->mt.state) != (r2->mt.next - r2->mt.state)) {
	return Qfalse;
    }
    if (r1->mt.left != r2->mt.left) {
	return Qfalse;
    }
    return Qtrue;
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
    struct MT *mt = &default_rnd()->mt;

    if (!genrand_initialized(mt)) {
	rand_init(mt, random_seed(0, 0));
    }
    if (argc == 0) {
	goto zero_arg;
    }
    VALUE vmax;
    rb_scan_args(argc, argv, "01", &vmax);
    if (NIL_P(vmax)) {
	goto zero_arg;
    }
    vmax = rb_to_int(vmax);
    VALUE r;
    if (vmax == INT2FIX(0) || NIL_P(r = rand_int(mt, vmax, 0))) {
zero_arg:
	return DBL2NUM(genrand_real(mt));
    }
    return r;
}

static st_index_t hashseed;

static void
Init_RandomSeed(void)
{
    VALUE random = random_alloc(0, 0);
    unsigned int initial[DEFAULT_SEED_CNT];
    fill_random_seed(initial);
    GC_WB(&get_rnd(random)->seed, make_seed_value(initial));
    rb_vm_set_default_random(random);

    struct MT *mt = &default_rnd()->mt;
    if (!genrand_initialized(mt)) {
	rand_init(mt, random_seed(0, 0));
    }

    hashseed = rb_genrand_int32();
#if SIZEOF_ST_INDEX_T*CHAR_BIT > 4*8
    hashseed <<= 32;
    hashseed |= rb_genrand_int32();
#endif
#if SIZEOF_ST_INDEX_T*CHAR_BIT > 8*8
    hashseed <<= 32;
    hashseed |= rb_genrand_int32();
#endif
#if SIZEOF_ST_INDEX_T*CHAR_BIT > 12*8
    hashseed <<= 32;
    hashseed |= rb_genrand_int32();
#endif
}

st_index_t
rb_hash_start(st_index_t h)
{
    return st_hash_start(hashseed + h);
}


void
rb_reset_random_seed(void)
{
    rb_random_t *r = default_rnd();
    uninit_genrand(&r->mt);
    r->seed = INT2FIX(0);
}

void
Init_Random(void)
{
    Init_RandomSeed();
    rb_objc_define_module_function(rb_mKernel, "srand", rb_f_srand, -1);
    rb_objc_define_module_function(rb_mKernel, "rand", rb_f_rand, -1);

    rb_cRandom = rb_define_class("Random", rb_cObject);
    rb_objc_define_method(*(VALUE *)rb_cRandom, "alloc", random_alloc, 0);
    rb_objc_define_method(rb_cRandom, "initialize", random_init, -1);
    rb_objc_define_method(rb_cRandom, "rand", random_rand, -1);
    rb_objc_define_method(rb_cRandom, "bytes", random_bytes, 1);
    rb_objc_define_method(rb_cRandom, "seed", random_get_seed, 0);
    rb_objc_define_method(rb_cRandom, "initialize_copy", random_copy, 1);
    rb_objc_define_method(rb_cRandom, "marshal_dump", random_dump, 0);
    rb_objc_define_method(rb_cRandom, "marshal_load", random_load, 1);
    rb_objc_define_private_method(rb_cRandom, "state", random_state, 0);
    rb_objc_define_private_method(rb_cRandom, "left", random_left, 0);
    rb_objc_define_method(rb_cRandom, "==", random_equal, 1);

    rb_objc_define_method(*(VALUE *)rb_cRandom, "srand", rb_f_srand, -1);
    rb_objc_define_method(*(VALUE *)rb_cRandom, "rand", rb_f_rand, -1);
    rb_objc_define_method(*(VALUE *)rb_cRandom, "new_seed", random_seed, 0);
    rb_objc_define_private_method(*(VALUE *)rb_cRandom, "state", random_s_state, 0);
    rb_objc_define_private_method(*(VALUE *)rb_cRandom, "left", random_s_left, 0);
}
