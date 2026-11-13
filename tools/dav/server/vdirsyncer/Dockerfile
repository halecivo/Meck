# vdirsyncer: copies iCloud contacts and calendars into Radicale (one way)
FROM python:3.12-slim
RUN pip install --no-cache-dir vdirsyncer==0.21.0 \
 && useradd --uid 1000 --create-home vdirsyncer
COPY sync.sh /usr/local/bin/sync.sh
USER vdirsyncer
ENTRYPOINT ["/bin/sh", "/usr/local/bin/sync.sh"]
