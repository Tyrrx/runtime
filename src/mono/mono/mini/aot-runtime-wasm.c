/**
 * \file
 * WASM AOT runtime
 */

#include "config.h"

#include <sys/types.h>

#include "mini.h"
#include <mono/jit/mono-private-unstable.h>
#include "interp/interp.h"
#include "aot-runtime.h"

#ifdef HOST_WASM

static char
type_to_c (MonoType *t)
{
	if (m_type_is_byref (t))
		return 'I';

handle_enum:
	switch (t->type) {
	case MONO_TYPE_BOOLEAN:
	case MONO_TYPE_CHAR:
	case MONO_TYPE_I1:
	case MONO_TYPE_U1:
	case MONO_TYPE_I2:
	case MONO_TYPE_U2:
	case MONO_TYPE_I4:
	case MONO_TYPE_U4:
	case MONO_TYPE_I:
	case MONO_TYPE_U:
	case MONO_TYPE_PTR:
	case MONO_TYPE_SZARRAY:
	case MONO_TYPE_CLASS:
	case MONO_TYPE_OBJECT:
	case MONO_TYPE_STRING:
		return 'I';
	case MONO_TYPE_R4:
		return 'F';
	case MONO_TYPE_R8:
		return 'D';
		break;
	case MONO_TYPE_I8:
	case MONO_TYPE_U8:
		return 'L';
	case MONO_TYPE_VOID:
		return 'V';
	case MONO_TYPE_VALUETYPE:
		if (m_class_is_enumtype (t->data.klass)) {
			t = mono_class_enum_basetype_internal (t->data.klass);
			goto handle_enum;
		}

		// https://github.com/WebAssembly/tool-conventions/blob/main/BasicCABI.md#function-signatures
		// Any struct or union that recursively (including through nested structs, unions, and arrays)
		//  contains just a single scalar value and is not specified to have greater than natural alignment.
		// FIXME: Handle the scenario where there are fields of struct types that contain no members
		MonoType *scalar_vtype;
		if (mini_wasm_is_scalar_vtype (t, &scalar_vtype))
			return type_to_c (scalar_vtype);

		return 'I';
	case MONO_TYPE_GENERICINST:
		if (m_class_is_valuetype (t->data.klass))
			return 'S';
		return 'I';
	default:
		g_warning ("CANT TRANSLATE %s", mono_type_full_name (t));
		return 'X';
	}
}

#define FIDX(x) (x)

typedef union {
	gint64 l;
	struct {
		gint32 lo;
		gint32 hi;
	} pair;
} interp_pair;

static gint64
get_long_arg (InterpMethodArguments *margs, int idx)
{
	interp_pair p;
	p.pair.lo = (gint32)(gssize)margs->iargs [idx];
	p.pair.hi = (gint32)(gssize)margs->iargs [idx + 1];
	return p.l;
}

static MonoWasmNativeToInterpCallback mono_wasm_interp_to_native_callback;

void
mono_wasm_install_interp_to_native_callback (MonoWasmNativeToInterpCallback cb)
{
	mono_wasm_interp_to_native_callback = cb;
}

int
mono_wasm_interp_method_args_get_iarg (InterpMethodArguments *margs, int i)
{
	return (int)(gssize)margs->iargs[i];
}

gint64
mono_wasm_interp_method_args_get_larg (InterpMethodArguments *margs, int i)
{
	return get_long_arg (margs, i);
}

float
mono_wasm_interp_method_args_get_farg (InterpMethodArguments *margs, int i)
{
	return *(float*)&margs->fargs [FIDX (i)];
}

double
mono_wasm_interp_method_args_get_darg (InterpMethodArguments *margs, int i)
{
	return margs->fargs [FIDX (i)];
}

gpointer*
mono_wasm_interp_method_args_get_retval (InterpMethodArguments *margs)
{
	return margs->retval;
}

static int
compare_icall_tramp (const void *key, const void *elem)
{
	return strcmp (key, *(void**)elem);
}

gpointer
mono_wasm_get_interp_to_native_trampoline (MonoMethodSignature *sig)
{
	char cookie [32];
	int c_count;

	c_count = sig->param_count + sig->hasthis + 1;
	g_assert (c_count < sizeof (cookie)); //ensure we don't overflow the local

	cookie [0] = type_to_c (sig->ret);
	if (sig->hasthis)
		cookie [1] = 'I';
	for (int i = 0; i < sig->param_count; ++i) {
		cookie [1 + sig->hasthis + i] = type_to_c (sig->params [i]);
	}
	cookie [c_count] = 0;

	void *p = mono_wasm_interp_to_native_callback (cookie);
	if (!p)
		g_error ("CANNOT HANDLE INTERP ICALL SIG %s\n", cookie);
	return p;
}

static MonoWasmGetNativeToInterpTramp get_native_to_interp_tramp_cb;

MONO_API void
mono_wasm_install_get_native_to_interp_tramp (MonoWasmGetNativeToInterpTramp cb)
{
	get_native_to_interp_tramp_cb = cb;
}

gpointer
mono_wasm_get_native_to_interp_trampoline (MonoMethod *method, gpointer extra_arg)
{
	if (get_native_to_interp_tramp_cb)
		return get_native_to_interp_tramp_cb (method, extra_arg);
	else
		return NULL;
}

#else /* TARGET_WASM */

void
mono_wasm_install_get_native_to_interp_tramp (MonoWasmGetNativeToInterpTramp cb)
{
	g_assert_not_reached ();
}

#endif /* TARGET_WASM */
