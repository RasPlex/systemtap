class returnstatement
{
    public static void printMessage(int message){}

    public static void printMessage(long message){}

    public static void printMessage(double message){}

    public static void printMessage(float message){}

    public static void printMessage(byte message){}

    public static void printMessage(boolean message){}

    public static void printMessage(char message){}

    public static void printMessage(short message){}

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

	printMessage(i);
	printMessage(j);
	printMessage(k);
	printMessage(l);
	printMessage(n);
	printMessage(o);
	printMessage(p);
	printMessage(q);

    }
}
