use dos86::fcb_util;
use dos86::types::*;

#[test]
fn test_getrec_basic() {
    let mut fcb = Fcb::default();
    fcb.nr = 3;
    fcb.extent = 0;
    let (rec, cx) = fcb_util::getrec_from_fcb(&fcb);
    assert_eq!(rec, 3);
    assert_eq!(cx, 1);
}

#[test]
fn test_getrec_zero() {
    let fcb = Fcb::default();
    let (rec, cx) = fcb_util::getrec_from_fcb(&fcb);
    assert_eq!(rec, 0);
    assert_eq!(cx, 1);
}

#[test]
fn test_setrndrec() {
    let mut fcb = Fcb::default();
    fcb.nr = 7;
    fcb.extent = 1;
    // SETRNDREC stores getrec result in RR field
    let (rec, _) = fcb_util::getrec_from_fcb(&fcb);
    fcb_util::setrndrec_in_fcb(&mut fcb, rec);
    // RR[0..2] = low 16 bits, RR[2] = bits 16-23
    let rr_val = fcb.rr[0] as u32 | ((fcb.rr[1] as u32) << 8) | ((fcb.rr[2] as u32) << 16);
    assert_eq!(rr_val, rec);
}
