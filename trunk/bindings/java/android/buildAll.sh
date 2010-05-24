#!/bin/bash
# Build tinyWRAP for Google Android Systems


for project in tinySAK tinyNET tinyIPSec tinySMS tinyHTTP tinySDP tinyMEDIA tinySIP
do
	echo -e building "$project....\n"
	make PROJECT=$project clean
	make PROJECT=$project BT=static all
done

echo building tinyWRAP....
make PROJECT=tinyWRAP clean
make PROJECT=tinyWRAP BT=shared all
