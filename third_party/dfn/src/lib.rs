//! A small C interface over DeepFilterNet 3 (libDF 0.5.6, MIT or Apache-2.0).
//!
//! The model works on 480 sample hops of 48 kHz mono audio scaled to [-1, 1].
//! Nothing here may unwind into C++, so every entry point catches panics.

use df::tract::{DfParams, DfTract, RuntimeParams};
use ndarray::{ArrayView2, ArrayViewMut2};
use std::panic::{catch_unwind, AssertUnwindSafe};

pub const HOP: usize = 480;

pub struct Dfn {
    model: DfTract,
}

/// Loads the embedded model. Returns null when it cannot be built or does
/// not run at 48 kHz with the expected hop.
/// A NaN argument keeps the model's own default. `atten_lim_db` caps how far
/// a frame is turned down; the three thresholds are local SNRs in dB that
/// pick the stages: below `min_db` a frame is zeroed, above `erb_db` it is
/// left alone, above `df_db` only the first stage runs.
#[no_mangle]
pub extern "C" fn slimm_dfn_create(
    atten_lim_db: f32,
    min_db: f32,
    erb_db: f32,
    df_db: f32,
) -> *mut Dfn {
    let built = catch_unwind(|| {
        let base = RuntimeParams::default_with_ch(1);
        let pick = |value: f32, default: f32| if value.is_nan() { default } else { value };
        let params = RuntimeParams::default_with_ch(1)
            .with_atten_lim(pick(atten_lim_db, base.atten_lim_db))
            .with_thresholds(
                pick(min_db, base.min_db_thresh),
                pick(erb_db, base.max_db_erb_thresh),
                pick(df_db, base.max_db_df_thresh),
            );
        let model = DfTract::new(DfParams::default(), &params).ok()?;
        if model.sr != 48_000 || model.hop_size != HOP || model.ch != 1 {
            return None;
        }
        Some(Box::new(Dfn { model }))
    });
    match built {
        Ok(Some(dfn)) => Box::into_raw(dfn),
        _ => std::ptr::null_mut(),
    }
}

/// Enhances one hop. `input` and `output` hold 480 samples and
/// may not overlap. Returns the frame's local SNR in dB, or NaN on failure.
///
/// # Safety
/// `dfn` comes from `slimm_dfn_create`; both buffers are valid for a hop.
#[no_mangle]
pub unsafe extern "C" fn slimm_dfn_process(
    dfn: *mut Dfn,
    input: *const f32,
    output: *mut f32,
) -> f32 {
    if dfn.is_null() || input.is_null() || output.is_null() {
        return f32::NAN;
    }
    let dfn = &mut *dfn;
    let result = catch_unwind(AssertUnwindSafe(|| {
        let noisy = ArrayView2::from_shape_ptr((1, HOP), input);
        let enhanced = ArrayViewMut2::from_shape_ptr((1, HOP), output);
        dfn.model.process(noisy, enhanced)
    }));
    match result {
        Ok(Ok(lsnr)) => lsnr,
        _ => f32::NAN,
    }
}

/// # Safety
/// `dfn` is null or comes from `slimm_dfn_create` and is not used again.
#[no_mangle]
pub unsafe extern "C" fn slimm_dfn_free(dfn: *mut Dfn) {
    if !dfn.is_null() {
        let _ = catch_unwind(AssertUnwindSafe(|| drop(Box::from_raw(dfn))));
    }
}
