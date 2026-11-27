ARG BASE_IMAGE=alpine:3.21
ARG BUILDER_IMAGE=alpine:3.21

FROM ${BUILDER_IMAGE} AS builder
RUN apk add --no-cache build-base cmake linux-headers
WORKDIR /src
COPY CMakeLists.txt version.txt ./
COPY src/ src/
RUN cmake -S . -B build \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_EXE_LINKER_FLAGS="-static" \
      -DBUILD_TESTING=OFF && \
    cmake --build build --parallel && \
    strip --strip-unneeded build/relaydx

FROM ${BASE_IMAGE}
WORKDIR /app
RUN apk add --no-cache ca-certificates tzdata && \
    ln -snf /usr/share/zoneinfo/Asia/Shanghai /etc/localtime && \
    echo "Asia/Shanghai" > /etc/timezone
ENV TZ=Asia/Shanghai \
    PATH="/app:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin"
COPY --from=builder /src/build/relaydx /app/relaydx
EXPOSE 67/udp 547/udp
ENTRYPOINT ["/app/relaydx"]
CMD ["--help"]
