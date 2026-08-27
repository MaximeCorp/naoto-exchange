.PHONY: bootstrap clean-artifacts

bootstrap:
	./bootstrap/run-all.sh

clean-artifacts:
	rm -rf artifacts/dpdk-* artifacts/etcd-cpp-apiv3-*