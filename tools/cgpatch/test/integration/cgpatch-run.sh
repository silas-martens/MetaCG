#!/bin/bash

function pretty {
	python3 -m json.tool < "$1" > "$1".json
	mv "$1".json "$1"
}

export OMPI_CXX=$(which clang++)
export LD_LIBRARY_PATH=/home/sm41myca/work/metacg-install/lib64/:$LD_LIBRARY_PATH


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
testerExe=$buildDir/tools/cgpatch/test/cgtester
cgmerge2Exe="$buildDir/tools/cgmerge2/cgmerge2"

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

testcase_prefixes=$(find "$inputDir" -name '*.cpp' | sed -E 's|.*/([0-9]+)_[^/]+\.cpp|\1|' | sort -u)
echo "$testcase_prefixes"
for prefix in $testcase_prefixes; do
    # Collect all files with this prefix
    testSources=$(find "$inputDir" -name "${prefix}_*.cpp" | sort)
    testName="${inputDir}/${prefix}" # base name without extension

    echo "testName = $testName"
    testGT="${testName}.gtpg"	# ground-truth dynamic patch-graph
    testPG="${testName}.pg"	# dynamic patch-graph
    testExe="${testName}.out"   # instrumented executable
    testSCG="${testName}.ipcg" # Static call.graph
    testMCG="${testName}.mcg"  # static merge call-graph
    export CGPATCH_CG_NAME="${testPG}"

    echo "testGT: $testGT"
    echo "testPG: $testPG"
    echo "testExe: $testExe"
    echo "Running testcase $testName with sources: $testSources"

    # Instrument using cgpatch
    #echo "$cgpatchExe mpicxx $testSources -o $testExe" 
    $cgpatchExe mpicxx $testSources -o "$testExe" >> "$logFile"
    if [ $? -ne 0 ]; then
        echo "Compilation failed for testcase $testName"
        fails=$((fails + 1))
        continue
    fi

    # Run test binary
    #echo "Running $testExe"
    "$testExe" >> "$logFile"
	if [ $? -ne 0 ]; then
        echo "Running instrumented binary failedd for testcase $testName"
        fails=$((fails + 1))
        continue
    fi

    # Evaluate output
    #echo "$testerExe $testPG $testGT >> $logFile"
    $testerExe "$testPG" "$testGT" >> "$logFile"
	if [ $? -ne 0 ]; then
		echo "$testPG and $testGT do not equal!"
		fails=$((fails + 1))
		continue
    fi

    # Merge patchgraph with static call-graph
	echo "Running $cgmerge2Exe $testMCG $testGT $testSCG >> $logFile"

    $cgmerge2Exe "$testMCG" "$testGT" "$testSCG" >> "$logFile"
	if [ $? -ne 0 ]; then
        echo "Merging static call-graph for testcase $testName failed"
        fails=$((fails + 1))
        continue
    fi



    if [ $? -ne 0 ]; then
        echo "Failure for file: $testPG. Keeping generated file for inspection"
        pretty "$testPG"
        fails=$((fails + 1))
    else
        rm "$testExe"
        rm "$testPG"
    fi


done


echo "Finished with $fails failure(s)."
