public class ReturnStringExample {

    public int calls = 0;

    public String compose(int value, boolean flag) {
        calls += 1;
        return flag ? "ok-" + value : "no-" + value;
    }

    public static void main(String[] args) {
        ReturnStringExample ex = new ReturnStringExample();
        String out = ex.compose(7, true);
        System.out.println("Out: " + out);
        System.out.println("Calls: " + ex.calls);
    }
}
