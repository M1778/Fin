const N: usize = 1000000;

fn partition(arr: &mut [i32], lo: i32, hi: i32) -> i32 {
    let pivot = arr[hi as usize];
    let mut i = lo - 1;
    for j in lo..hi {
        if arr[j as usize] < pivot {
            i += 1;
            arr.swap(i as usize, j as usize);
        }
    }
    arr.swap((i + 1) as usize, hi as usize);
    i + 1
}

fn quicksort(arr: &mut [i32], lo: i32, hi: i32) {
    if lo < hi {
        let p = partition(arr, lo, hi);
        quicksort(arr, lo, p - 1);
        quicksort(arr, p + 1, hi);
    }
}

fn main() {
    let mut arr: Vec<i32> = Vec::with_capacity(N);
    let mut seed: u64 = 12345;
    for _ in 0..N {
        seed = seed.wrapping_mul(1103515245).wrapping_add(12345);
        arr.push(((seed / 65536) % 1000000) as i32);
    }
    quicksort(&mut arr, 0, N as i32 - 1);
    println!("{}", arr[N / 2]);
}
