#include <ruby.h>
#include <stdio.h>
#include <stdlib.h>
#include <librsync.h>

VALUE RRDiff = Qnil;

static void close_if_open(FILE **f) {
    if (f && *f) { fclose(*f); *f = NULL; }
}

static VALUE rrdiff_signature(VALUE mod, VALUE old_file, VALUE sig_file)
{
    FILE *basis = NULL, *signature = NULL;
    rs_stats_t stats;
    rs_result result;

    basis = fopen(StringValuePtr(old_file), "rb");
    if (!basis) rb_sys_fail(StringValuePtr(old_file));

    signature = fopen(StringValuePtr(sig_file), "wb");
    if (!signature) {
        fclose(basis);
        rb_sys_fail(StringValuePtr(sig_file));
    }

    result = rs_sig_file(basis, signature,
                         RS_DEFAULT_BLOCK_LEN,
                         RS_DEFAULT_MIN_STRONG_LEN,
                         RS_BLAKE2_SIG_MAGIC,
                         &stats);

    fclose(basis);
    fclose(signature);

    if (result != RS_DONE) {
        rb_raise(rb_eRuntimeError, "librsync rs_sig_file failed (%d)", (int)result);
    }

    return Qnil;
}

static VALUE rrdiff_delta(VALUE mod, VALUE new_file, VALUE sig_file, VALUE delta_file)
{
    FILE *newfile = NULL, *sigfile = NULL, *deltafile = NULL;
    rs_stats_t stats;
    rs_signature_t *sig = NULL;
    rs_result result;

    newfile = fopen(StringValuePtr(new_file), "rb");
    if (!newfile) rb_sys_fail(StringValuePtr(new_file));

    sigfile = fopen(StringValuePtr(sig_file), "rb");
    if (!sigfile) { close_if_open(&newfile); rb_sys_fail(StringValuePtr(sig_file)); }

    deltafile = fopen(StringValuePtr(delta_file), "wb");
    if (!deltafile) { close_if_open(&sigfile); close_if_open(&newfile); rb_sys_fail(StringValuePtr(delta_file)); }

    result = rs_loadsig_file(sigfile, &sig, &stats);
    if (result != RS_DONE) goto cleanup;

    result = rs_build_hash_table(sig);
    if (result != RS_DONE) goto cleanup;

    result = rs_delta_file(sig, newfile, deltafile, &stats);

cleanup:
    if (sig) rs_free_sumset(sig);
    close_if_open(&deltafile);
    close_if_open(&sigfile);
    close_if_open(&newfile);

    if (result != RS_DONE) {
        rb_raise(rb_eRuntimeError, "librsync delta failed (%d)", (int)result);
    }

    return Qnil;
}

static VALUE rrdiff_patch(VALUE mod, VALUE old_file, VALUE delta_file, VALUE patched_file)
{
    FILE *basisfile = NULL, *deltafile = NULL, *newfile = NULL;
    rs_stats_t stats;
    rs_result result;

    basisfile = fopen(StringValuePtr(old_file), "rb");
    if (!basisfile) rb_sys_fail(StringValuePtr(old_file));

    deltafile = fopen(StringValuePtr(delta_file), "rb");
    if (!deltafile) { close_if_open(&basisfile); rb_sys_fail(StringValuePtr(delta_file)); }

    newfile = fopen(StringValuePtr(patched_file), "wb");
    if (!newfile) { close_if_open(&deltafile); close_if_open(&basisfile); rb_sys_fail(StringValuePtr(patched_file)); }

    result = rs_patch_file(basisfile, deltafile, newfile, &stats);

    close_if_open(&newfile);
    close_if_open(&deltafile);
    close_if_open(&basisfile);

    if (result != RS_DONE) {
        rb_raise(rb_eRuntimeError, "librsync patch failed (%d)", (int)result);
    }

    return Qnil;
}

void Init_rrdiff(void)
{
    RRDiff = rb_define_module("RRDiff");
    rb_define_singleton_method(RRDiff, "signature", rrdiff_signature, 2);
    rb_define_singleton_method(RRDiff, "delta", rrdiff_delta, 3);
    rb_define_singleton_method(RRDiff, "patch", rrdiff_patch, 3);
}