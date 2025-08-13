.PHONY: run build setup clean

BUILD_DIR=build

all: build

setup:
	cmake -B ${BUILD_DIR} -DCMAKE_BUILD_TYPE=Release --fresh

run: build
	${BUILD_DIR}/flprox

build:
	cmake --build ${BUILD_DIR}

clean:
	rm -rf ${BUILD_DIR}