# The game server's image (ticket 14). Built on the VPS by server/deploy/compose.yaml; the
# access-key config is mounted at run time and never enters the image.
FROM golang:1.27.1-bookworm AS build
WORKDIR /src
COPY go.mod go.sum ./
COPY third_party ./third_party
RUN go mod download
COPY cmd ./cmd
COPY internal ./internal
RUN CGO_ENABLED=0 GOTOOLCHAIN=local go build -trimpath -ldflags=-s -o /allstars-server ./cmd/allstars-server

FROM gcr.io/distroless/static-debian12:nonroot
LABEL org.opencontainers.image.title="allstars-server"
COPY --from=build /allstars-server /allstars-server
# nex-go's logger writes ./log here (a tmpfs in compose.yaml).
WORKDIR /home/nonroot
ENTRYPOINT ["/allstars-server"]
CMD ["serve", "-config", "/config/allstars-server.json"]
