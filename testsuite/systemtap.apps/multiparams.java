class multiparams
{
    public static void printMessage1(int a){}
    public static void printMessage2(long a, int b){}
    public static void printMessage3(double a, long b, int c){}
    public static void printMessage4(float a, double b, long c, int d){}
    public static void printMessage5(byte a, float b, double c, long d, int e){}
    public static void printMessage6(boolean a, byte b, float c, double d, long e, int f){}
    public static void printMessage7(char a, boolean b, byte c, float d, double e, long f, int g){}
    public static void printMessage8(short a, char b, boolean c, byte d, float e, double f, long g, int h){}
    public static void printMessage9(short a, short b, char c, boolean d, byte e, float f, double g, long h, int i){}
    public static void printMessage10(short a, short b, short c, char d, boolean e, byte f, float g, double h, long i, int j){}

    public static void main(String[] args) throws InterruptedException
    {
	
	Thread.sleep(20000);
	final int i = 42;
	final long j = 254775806;
	final double k = 3.14;
	final float l = 2345987;
	final byte n = 10;
	final boolean o = true;
	final char p = 'a';
	final short q = 14;

	printMessage1(i);
	printMessage2(j, i);
	printMessage3(k, j, i);
	printMessage4(l, k, j, i);
	printMessage5(n, l, k, j, i);
	printMessage6(o, n, l, k, j, i);
	printMessage7(p, o, n, l, k, j, i);
	printMessage8(q, p, o, n, l, k, j, i);
	printMessage9(q, q, p, o, n, l, k, j, i);
	printMessage10(q, q, q, p, o, n, l, k, j, i);

    }
}
