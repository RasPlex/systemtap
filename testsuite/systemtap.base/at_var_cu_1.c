extern void foo(void);
extern void bar(void);

static int main_global = 5;

int
main(void)
{
  foo();
  bar();
  baz();
  bah();
  return 0;
}
