#!/bin/bash


fext=".txt"
difftext="diff"
maxtest=48
diffdir="diffOutputs/"
fname="results"
resultsdir="testResults/"
myresultsdir="myResults/"


diffresults="$(diff $resultsdir$1$fext $myresultsdir$1$fname$fext)"
diffsize=${#diffresults}

if [ $diffsize -gt 0 ]; then
         echo "FAILED"
         diff $resultsdir$1$fext $myresultsdir$1$fname$fext
   else
         echo "SUCCEEDED"
fi
