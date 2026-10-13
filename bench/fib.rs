fn fib(n: i64) -> i64 {
    if n <= 1 { return n; }
    fib(n-1) + fib(n-2)
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    let n: i64 = if args.len() > 1 { args[1].parse().unwrap() } else { 40 };
    println!("{}", fib(n));
}
