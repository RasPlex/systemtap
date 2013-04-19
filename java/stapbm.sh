#!/bin/bash

# $1 - install/uninstall
# $2 - PID/unique name
# $3 - RULE name
# $4 - class
# $5 - method
# $6 - number of args
# $7 - entry/exit/line
# $8 - $STAPTMPDIR

#We need to both record, and check that this pid is the first
#This is to avoid recurssion in probing java processes
function check_running()
{
    FILE=`hostname`-$$
    JAVA_FILE=`hostname`-$2
    if [ -f $1/java/$FILE ]
    then
	exit
    else
	touch $1/java/$FILE
    fi
    if [ -f $1/java/$JAVA_FILE ]
    then
	exit
    else
	touch $1/java/$JAVA_FILE
    fi

}
function run_byteman()
{
    case $1 in
	install)
	    exec bminstall.sh $2 &
	    pid=$!
	    echo $?
	    wait $pid
	    exec bmsubmit.sh -l $4/java/byteman-test.btm &
	    pida=$!
	    echo $?
	    wait $pida
	    ;;
	uninstall)
	    exec bmsubmit.sh -u $4/java/byteman-test.btm
	    exec rm $4/java/$FILE
	    ;;
	*)
	    echo "this should never be hit"
	    ;;
    esac

}

#function for where to execute the script
function script_invocation_location()
{
    case "$1" in
	entry)
	    echo "AT ENTRY" >> $2/java/byteman-test.btm
	    ;;
	retur*)
	    echo "AT RETURN" >> $2/java/byteman-test.btm
	    ;;
	*)
	    echo "AT LINE $1" >> $2/java/byteman-test.btm
	    ;;
    esac
}

#function with case
function print_stap_helper
{
  case "$1" in
      0)
	  echo "DO METHOD_STAP_PROBE0(\"$2\", \"$3\")" >> $4/java/byteman-test.btm
	  ;;
      1)
	  echo "DO METHOD_STAP_PROBE1(\"$2\", \"$3\", \$1)" >> $4/java/byteman-test.btm
	  ;;
      2)
	  echo "DO METHOD_STAP_PROBE2(\"$2\", \"$3\", \$1, \$2)" >> $4/java/byteman-test.btm
	  ;;
      3)
	  echo "DO METHOD_STAP_PROBE3(\"$2\", \"$3\", \$1, \$2, \$3)" >> $4/java/byteman-test.btm
	  ;;
      4)
	  echo "DO METHOD_STAP_PROBE4(\"$2\", \"$3\", \$1, \$2, \$3, \$4)" >> $4/java/byteman-test.btm
	  ;;
      5)
	  echo "DO METHOD_STAP_PROBE5(\"$2\", \"$3\", \$1, \$2, \$3, \$4, \$5)" >> $4/java/byteman-test.btm
	  ;;
      6)
	  echo "DO METHOD_STAP_PROBE6(\"$2\", \"$3\", \$1, \$2, \$3, \$4, \$5, \$6)" >> $4/java/byteman-test.btm
	  ;;
      7)
	  echo "DO METHOD_STAP_PROBE7(\"$2\", \"$3\", \$1, \$2, \$3, \$4, \$5, \$6, \$7)" >> $4/java/byteman-test.btm
	  ;;
      8)
	  echo "DO METHOD_STAP_PROBE8(\"$2\", \"$3\", \$1, \$2, \$3, \$4, \$5, \$6, \$7, \$8)" >> $4/java/byteman-test.btm
	  ;;
      9)
	  echo "DO METHOD_STAP_PROBE9(\"$2\", \"$3\", \$1, \$2, \$3, \$4, \$5, \$6, \$7, \$8, \$9)" >> $4/java/byteman-test.btm
	  ;;
      10)
	  echo "DO METHOD_STAP_PROBE10(\"$2\", \"$3\", \$1, \$2, \$3, \$4, \$5, \$6, \$7, \$8, \$9, \$10)" >> $4/java/byteman-test.btm
	  ;;
      *)
	  echo "DO METHOD_STAP_PROBE0(\"$2\", \"$3\")" >> $4/java/byteman-test.btm	  
	  ;;
  esac
}

mkdir -p $8/java
check_running $8 $2
echo "RULE $3" >> $8/java/byteman-test.btm
echo "CLASS $4" >> $8/java/byteman-test.btm
echo "METHOD $5" >> $8/java/byteman-test.btm
echo "HELPER HelperSDT" >> $8/java/byteman-test.btm
# at what begin, end, line?
script_invocation_location $7 $8

echo "IF TRUE" >> $8/java/byteman-test.btm
# stap helper method switch statement
print_stap_helper $6 $4 $5 $8
echo "ENDRULE" >> $8/java/byteman-test.btm

#first we should check to even run the rule
run_byteman $1 $2 $3 $8 

