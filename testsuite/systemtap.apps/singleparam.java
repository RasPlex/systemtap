class singleparam
{
    public static void printMessage(int message)
    {
	//		System.out.println("int: " + message);
    }
    public static void printMessage(long message)
    {
	//	System.out.println("long: " + message);
    }
    public static void printMessage(double message)
    {
	//	System.out.println("double: " + message);
    }
    public static void printMessage(float message)
    {
	//	System.out.println("float: " + message);
    }
    public static void printMessage(byte message)
    {
	//	System.out.println("byte: " + message);
    }
    public static void printMessage(boolean message)
    {
	//	System.out.println("boolean: " + message);
    }
    public static void printMessage(char message)
    {
	//	System.out.println("char: " + message);
    }
    public static void printMessage(short message)
    {
	//	System.out.println("short: " + message);
    }


    public static void main(String[] args) throws InterruptedException
    {
	
	Thread.sleep(30000);
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
