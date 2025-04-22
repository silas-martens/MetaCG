#!/bin/bash

function pretty {
	python3 -m json.tool < "$1" > "$1".json
	mv "$1".json "$1"
}

build_dir=build # default

while getopts ":b:h" opt; do
	case $opt in
		b)
			if [ -z $OPTARG ]; then
				echo "no build directory given, assuming \"build\""
			fi
			build_dir=$OPTARG
			;;
		h)
			echo "use -b to provide build directory NAME"
			echo "use -h to print this help"
			exit 0;
			;;
		\?)
			echo "Invalid option -$OPTARG"
			exit 1;
			;;
	esac
done

fails=0
logDir=$PWD/logging
logFile=$logDir/cgpatch-${CI_CONCURRENT_ID}.log
inputDir=$PWD/input
buildDir=$PWD/../../../../${build_dir}/
cgpatchExe=$buildDir/cgcollector/tools/wrapper/patchcxx
testerExe=$buildDir/cgcollector/test/cgtester

# clean up
if [ ! -d ${logDir} ]; then
	mkdir ${logDir}
fi
echo "" > ${logFile}

# check if cgpatch wrapper is available
if ! type $executable > /dev/null; then
	echo "Cannot find $executable"
	exit
else
	echo "Found $executable"
	testNo=$(($testNo+1))
fi

# Running testcases
testcases=$inputDir/*.cpp
for testcase in $testcases; do
	# Setup test variables
	testName="${testcase%.*}"
	testGT=${testName}.gtpg
	testPG="${testName}.pg"
	testExe="${testName}.out"
	export CGPATCH_CG_NAME="${testPG}"
	
	echo "Running testcase $testName."
	
	# Generate patch-graph
	# FIX: remove -stdlib=libstdc++
	$cgpatchExe mpicxx $testcase -stdlib=libstdc++ -o "${testName}.out" # Compile testcase
	${testName}.out # Run testcase / Generate patch-graph

	$testerExe $testPG $testGT >> $logFile # Evaluate testcase

	if [ $? -ne 0 ]; then
		echo "Failure for file: $testPG. Keeping generated file for inspection"
		pretty $testPG
		fails=$((fails + 1))
	else
		rm $testExe
		rm $testPG
	fi
done


echo "Finished with $fails failure(s)."
