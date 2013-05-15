struct s1
{
  char c;
  short s;
  int i;
  long l;
  float f;
  double d;
};

struct s2
{
  double d;
  float f;
  long l;
  int i;
  short s;
  char c;
};

s1 S1;
s2 S2;

int func1 (s1 *p)
{
  return p->i;
}

int func2 (s2 *q)
{
  return q->i;
}

int main()
{
  return func1 (&S1) - func2 (&S2);
}
