#!/bin/bash

array=($(ls testcases))
fname="results"
resultsdir="testResults/"
myresultsdir="myResults/"
fext=".txt"
difftext="diff"
maxtest=48
diffdir="diffOutputs/"

rm myResults/* &> /dev/null
rm diffOutputs/* &> /dev/null
mkdir myResults &> /dev/null
mkdir diffOutputs &> /dev/null


count=0

for i in "${array[@]}"
do
   :

   test="$(cut -d'.' -f1 <<< $i)"
   echo -n "Running $test ....................... "

   make $test &> /dev/null
   eval ./$test &> $myresultsdir$test$fname$fext
   diffresults="$(diff $resultsdir$test$fext $myresultsdir$test$fname$fext)"
   diffsize=${#diffresults}

   if [ $diffsize -gt 0 ]; then
   		echo "FAILED"
         diff $resultsdir$test$fext $myresultsdir$test$fname$fext &> $diffdir$test$difftext$fext
   else 
   		echo "SUCCEEDED"
		diff $resultsdir$test$fext $myresultsdir$test$fname$fext &> $diffdir$test$difftext$fext
   fi

   if [ $count -gt $(expr $maxtest - 1) ]
   then
   		break
   	fi

	count=$(expr $count + 1)

done

make clean &> /dev/null

