import java.util.Scanner;

public class SimpleClass {

    public int sorokDva = 42;
    public boolean False = false;
    public boolean True = true;
    public int[] Primes = {2, 3, 5, 7, 11};
    private SimpleClass ref;

    public int increment(int number, boolean flag) {
        this.sorokDva += 42;
        int kek = 32;
        if (flag) {
            number += 1;
        }
        return number + kek;
    }

    public static void main(String[] args) {

        Scanner in = new Scanner(System.in);
        int num = in.nextInt();
        SimpleClass np = new SimpleClass();
        np.ref = np;
        int result = np.increment(num, true);
        System.out.println("Res: " + result);
    }
}
