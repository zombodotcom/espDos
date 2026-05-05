use dos86::types::*;
use dos86::*;

fn make_dpb(secsiz: u16, clusmsk: u8, fatcnt: u8, fatsiz: u8, maxclus: u16) -> Dpb {
    Dpb {
        drvnum: 0,
        secsiz,
        clusmsk,
        clusshft: 0,
        firfat: 1,
        fatcnt,
        maxent: 64,
        firrec: 8,
        maxclus,
        fatsiz,
        firdir: 3,
        firrec1: 0,
        maxclus1: 0,
        firrec2: 0,
        maxclus2: 0,
        dirtyfat: 0xFF,
        dirsiz: 0,
        fat: vec![0u8; 512],
    }
}

#[test]
fn test_unpack_even() {
    let mut dpb = make_dpb(512, 0, 2, 2, 100);
    // cluster 4 even: bytes [6,7], low 12 bits
    dpb.fat[6] = 0x05;
    dpb.fat[7] = 0x00;
    let val = fat::unpack(&dpb.fat, dpb.maxclus, 4).unwrap();
    assert_eq!(val, 5);
}

#[test]
fn test_unpack_odd() {
    let mut dpb = make_dpb(512, 0, 2, 2, 100);
    // cluster 3 odd: bytes [4,5], upper 12 bits
    dpb.fat[4] = 0x50;
    dpb.fat[5] = 0x00;
    let val = fat::unpack(&dpb.fat, dpb.maxclus, 3).unwrap();
    assert_eq!(val, 5);
}

#[test]
fn test_pack_unpack_roundtrip() {
    let mut fat_data = vec![0u8; 512];
    fat::pack(&mut fat_data, 2, 0x123);
    let val = fat::unpack(&fat_data, 200, 2).unwrap();
    assert_eq!(val, 0x123);
}

#[test]
fn test_pack_eof() {
    let mut fat_data = vec![0u8; 512];
    fat::pack(&mut fat_data, 5, 0xFFF);
    let val = fat::unpack(&fat_data, 200, 5).unwrap();
    assert_eq!(val, 0xFFF);
}

#[test]
fn test_pack_free() {
    let mut fat_data = vec![0xFF_u8; 512];
    fat::pack(&mut fat_data, 6, 0x000);
    let val = fat::unpack(&fat_data, 200, 6).unwrap();
    assert_eq!(val, 0);
}
