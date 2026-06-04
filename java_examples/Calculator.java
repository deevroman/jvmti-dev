public class Calculator {

    public int cnt = 0;

    public int add(int a, int b) {
        this.cnt += 1;
        return a + b;
    }

    public static void main(String[] args) {
        Calculator calc = new Calculator();
        int result = calc.add(2, 10000);
        System.out.println("Result: " + result);
        System.out.println("Count: " + calc.cnt);
    }
}
