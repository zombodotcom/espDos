use dos86::fcb_util;
use dos86::types::*;

#[test]
fn test_getrec_nr() {
    // GETREC: DI=FCB, NR=5, EXTENT=0 → record = 5, CX=1
    let mut fcb = Fcb::default();
    fcb.nr = 5;
    fcb.extent = 0;
    let (rec, cx) = fcb_util::getrec_from_fcb(&fcb);
    assert_eq!(rec, 5u32);
    assert_eq!(cx, 1u16);
}

#[test]
fn test_getrec_extent() {
    // NR=0, EXTENT=2 → record = 128*2 = 256
    let mut fcb = Fcb::default();
    fcb.nr = 0;
    fcb.extent = 2;
    let (rec, _) = fcb_util::getrec_from_fcb(&fcb);
    assert_eq!(rec, 256u32);
}
