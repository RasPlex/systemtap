#!/usr/bin/bash

# $1 - PID/unique name
# $2 - class
# $3 - method
# $4 - number of args
# $5 - install/uninstall
# $6 - entry/exit/line

#function for where to execute the script
function script_invocation_location()
{
    case "$1" in
	entry)
	    echo "AT ENTRY" >> byteman-test.btm
	    ;;
	retur*)
	    echo "AT RETRUN" >> byteman-test.btm
	    ;;
	*)
	    echo "AT LINE $1" >> byteman-test.btm
	    ;;
    esac
}

#function with case
function print_stap_helper
{
  case "$1" in
      0)
	  echo "DO HELPER_STAP_PROBE0($2, $3)" >> byteman-test.btm
	  ;;
      1)
	  echo "DO HELPER_STAP_PROBE1($2, $3, \$1)" >> byteman-test.btm
	  ;;
      2)
	  echo "DO HELPER_STAP_PROBE2($2, $3, \$1, \$2)" >> byteman-test.btm
	  ;;
      3)
	  echo "DO HELPER_STAP_PROBE3($2, $3, \$1, \$2, \$3)" >> byteman-test.btm
	  ;;
      4)
	  echo "DO HELPER_STAP_PROBE4($2, $3, \$1, \$2, \$3, \$4)" >> byteman-test.btm
	  ;;
      5)
	  echo "DO HELPER_STAP_PROBE5($2, $3, \$1, \$2, \$3, \$4, \$5)" >> byteman-test.btm
	  ;;
      6)
	  echo "DO HELPER_STAP_PROBE6($2, $3, \$1, \$2, \$3, \$4, \$5, \$6)" >> byteman-test.btm
	  ;;
      7)
	  echo "DO HELPER_STAP_PROBE7($2, $3, \$1, \$2, \$3, \$4, \$5, \$6, \$7)" >> byteman-test.btm
	  ;;
      8)
	  echo "DO HELPER_STAP_PROBE8($2, $3, \$1, \$2, \$3, \$4, \$5, \$6, \$7, \$8)" >> byteman-test.btm
	  ;;
      9)
	  echo "DO HELPER_STAP_PROBE9($2, $3, \$1, \$2, \$3, \$4, \$5, \$6, \$7, \$8, \$9)" >> byteman-test.btm
	  ;;
      10)
	  echo "DO HELPER_STAP_PROBE10($2, $3, \$1, \$2, \$3, \$4, \$5, \$6, \$7, \$8, \$9, \$10)" >> byteman-test.btm
	  ;;
      *)
	  echo "DO HELPER_STAP_PROBE0($2, $3)" >> byteman-test.btm	  
	  ;;
      esac
}
echo "RULE test" >> byteman-test.btm
echo "CLASS $2" >> byteman-test.btm
echo "METHOD $3" >> byteman-test.btm
echo "HELPER HelperSDT" >> byteman-test.btm
# at what begin, end, line?
script_invocation_location $6

echo "IF TRUE" >> byteman-test.btm
# stap helper method switch statement
print_stap_helper $4 $2 $3
echo "ENDRULE" >> byteman-test.btm

