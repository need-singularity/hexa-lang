// GENERATED FROM self/runtime_hi.hexa — do not edit manually.
// Source of truth: self/runtime_hi.hexa (M1-lite hi-layer SSOT).
// Reproduce: tool/extract_runtime_hi.sh (runs self/native/hexa_v2 then
// strips main/selftest, renames __hexa_sl_* -> __hexa_rt_sl_*, and marks
// rt_str_* as static).
// Included from self/runtime.c AFTER hexa_str_join is defined.
// (hxa-20260423-003 Step 4 — extraction replaces hand-port rt_str_*)

static HexaVal __hexa_rt_sl_0;
static HexaVal __hexa_rt_sl_1;
static HexaVal __hexa_rt_sl_2;
static HexaVal __hexa_rt_sl_3;
static HexaVal __hexa_rt_sl_4;
static HexaVal __hexa_rt_sl_5;
static HexaVal __hexa_rt_sl_6;
static HexaVal __hexa_rt_sl_7;
static HexaVal __hexa_rt_sl_8;
static HexaVal __hexa_rt_sl_9;
static HexaVal __hexa_rt_sl_10;
static HexaVal __hexa_rt_sl_11;
static HexaVal __hexa_rt_sl_12;
static HexaVal __hexa_rt_sl_13;
static void __hexa_rt_strlit_init(void) {
    __hexa_rt_sl_0 = hexa_str("\n");
    __hexa_rt_sl_1 = hexa_str("");
    __hexa_rt_sl_2 = hexa_str("7");
    __hexa_rt_sl_3 = hexa_str("0");
    __hexa_rt_sl_4 = hexa_str("ab");
    __hexa_rt_sl_5 = hexa_str("hi");
    __hexa_rt_sl_6 = hexa_str("-");
    __hexa_rt_sl_7 = hexa_str("a\nb\nc");
    __hexa_rt_sl_8 = hexa_str("007");
    __hexa_rt_sl_9 = hexa_str("700");
    __hexa_rt_sl_10 = hexa_str("ababab");
    __hexa_rt_sl_11 = hexa_str("--hi--");
    __hexa_rt_sl_12 = hexa_str("runtime_hi: selftest OK");
    __hexa_rt_sl_13 = hexa_str("runtime_hi: selftest FAIL");
}

static HexaVal rt_str_split(HexaVal s, HexaVal delim) {
    __hexa_fn_arena_enter();
    HexaVal out = hexa_array_new();
    if (hexa_truthy(hexa_eq(hexa_int(hexa_len(delim)), hexa_int(0)))) {
        hexa_array_push(out, s);
        return __hexa_fn_arena_return(out);
    }
    HexaVal dlen = hexa_int(hexa_len(delim));
    HexaVal start = hexa_int(0);
    HexaVal i = hexa_int(0);
    while (HX_BOOL(hexa_cmp_le(i, hexa_sub(hexa_int(hexa_len(s)), dlen)))) {
        if (hexa_truthy(hexa_eq(hexa_str_substring(s, i, hexa_add(i, dlen)), delim))) {
            hexa_array_push(out, hexa_str_substring(s, start, i));
            start = hexa_add(i, dlen);
            i = start;
        } else {
            i = hexa_add(i, hexa_int(1));
        }
    }
    hexa_array_push(out, hexa_str_substring(s, start, hexa_int(hexa_len(s))));
    return __hexa_fn_arena_return(out);
    return __hexa_fn_arena_return(hexa_void());
}


static HexaVal rt_str_lines(HexaVal s) {
    __hexa_fn_arena_enter();
    return __hexa_fn_arena_return(rt_str_split(s, __hexa_rt_sl_0));
    return __hexa_fn_arena_return(hexa_void());
}


static HexaVal rt_str_pad_left(HexaVal s, HexaVal width, HexaVal pad) {
    __hexa_fn_arena_enter();
    HexaVal slen = hexa_int(hexa_len(s));
    HexaVal plen = hexa_int(hexa_len(pad));
    if (hexa_truthy(hexa_cmp_ge(slen, width))) {
        return __hexa_fn_arena_return(s);
    }
    if (hexa_truthy(hexa_eq(plen, hexa_int(0)))) {
        return __hexa_fn_arena_return(s);
    }
    HexaVal pad_total = hexa_sub(width, slen);
    HexaVal iters = hexa_div(pad_total, plen);
    if (hexa_truthy(hexa_cmp_lt(hexa_mul(iters, plen), pad_total))) {
        iters = hexa_add(iters, hexa_int(1));
    }
    HexaVal parts = hexa_array_new();
    HexaVal i = hexa_int(0);
    while (HX_BOOL(hexa_cmp_lt(i, iters))) {
        hexa_array_push(parts, pad);
        i = hexa_add(i, hexa_int(1));
    }
    hexa_array_push(parts, s);
    return __hexa_fn_arena_return(hexa_str_join(parts, __hexa_rt_sl_1));
    return __hexa_fn_arena_return(hexa_void());
}


static HexaVal rt_str_pad_right(HexaVal s, HexaVal width, HexaVal pad) {
    __hexa_fn_arena_enter();
    HexaVal slen = hexa_int(hexa_len(s));
    HexaVal plen = hexa_int(hexa_len(pad));
    if (hexa_truthy(hexa_cmp_ge(slen, width))) {
        return __hexa_fn_arena_return(s);
    }
    if (hexa_truthy(hexa_eq(plen, hexa_int(0)))) {
        return __hexa_fn_arena_return(s);
    }
    HexaVal pad_total = hexa_sub(width, slen);
    HexaVal iters = hexa_div(pad_total, plen);
    if (hexa_truthy(hexa_cmp_lt(hexa_mul(iters, plen), pad_total))) {
        iters = hexa_add(iters, hexa_int(1));
    }
    HexaVal parts = hexa_array_new();
    hexa_array_push(parts, s);
    HexaVal i = hexa_int(0);
    while (HX_BOOL(hexa_cmp_lt(i, iters))) {
        hexa_array_push(parts, pad);
        i = hexa_add(i, hexa_int(1));
    }
    return __hexa_fn_arena_return(hexa_str_join(parts, __hexa_rt_sl_1));
    return __hexa_fn_arena_return(hexa_void());
}


static HexaVal rt_str_repeat(HexaVal s, HexaVal count) {
    __hexa_fn_arena_enter();
    if (hexa_truthy(hexa_cmp_le(count, hexa_int(0)))) {
        return __hexa_fn_arena_return(__hexa_rt_sl_1);
    }
    HexaVal parts = hexa_array_new();
    HexaVal i = hexa_int(0);
    while (HX_BOOL(hexa_cmp_lt(i, count))) {
        hexa_array_push(parts, s);
        i = hexa_add(i, hexa_int(1));
    }
    return __hexa_fn_arena_return(hexa_str_join(parts, __hexa_rt_sl_1));
    return __hexa_fn_arena_return(hexa_void());
}


static HexaVal rt_str_center(HexaVal s, HexaVal width, HexaVal pad) {
    __hexa_fn_arena_enter();
    HexaVal slen = hexa_int(hexa_len(s));
    HexaVal plen = hexa_int(hexa_len(pad));
    if (hexa_truthy(hexa_cmp_ge(slen, width))) {
        return __hexa_fn_arena_return(s);
    }
    if (hexa_truthy(hexa_eq(plen, hexa_int(0)))) {
        return __hexa_fn_arena_return(s);
    }
    HexaVal total_pad = hexa_sub(width, slen);
    HexaVal left_pad = hexa_div(total_pad, hexa_int(2));
    HexaVal right_pad = hexa_sub(total_pad, left_pad);
    HexaVal li = hexa_div(left_pad, plen);
    if (hexa_truthy(hexa_cmp_lt(hexa_mul(li, plen), left_pad))) {
        li = hexa_add(li, hexa_int(1));
    }
    HexaVal ri = hexa_div(right_pad, plen);
    if (hexa_truthy(hexa_cmp_lt(hexa_mul(ri, plen), right_pad))) {
        ri = hexa_add(ri, hexa_int(1));
    }
    HexaVal parts = hexa_array_new();
    HexaVal i = hexa_int(0);
    while (HX_BOOL(hexa_cmp_lt(i, li))) {
        hexa_array_push(parts, pad);
        i = hexa_add(i, hexa_int(1));
    }
    hexa_array_push(parts, s);
    HexaVal j = hexa_int(0);
    while (HX_BOOL(hexa_cmp_lt(j, ri))) {
        hexa_array_push(parts, pad);
        j = hexa_add(j, hexa_int(1));
    }
    return __hexa_fn_arena_return(hexa_str_join(parts, __hexa_rt_sl_1));
    return __hexa_fn_arena_return(hexa_void());
}



__attribute__((constructor))
static void __hexa_rt_strlit_init_ctor(void) { __hexa_rt_strlit_init(); }
