const N: usize = 200;

fn main() {
    let mut a = vec![vec![0i64; N]; N];
    let mut b = vec![vec![0i64; N]; N];
    let mut c = vec![vec![0i64; N]; N];
    for i in 0..N {
        for j in 0..N {
            a[i][j] = (i + j) as i64;
            b[i][j] = (i as i64) - (j as i64);
        }
    }
    for i in 0..N {
        for k in 0..N {
            let aik = a[i][k];
            for j in 0..N {
                c[i][j] += aik * b[k][j];
            }
        }
    }
    println!("{}", c[N-1][N-1]);
}
