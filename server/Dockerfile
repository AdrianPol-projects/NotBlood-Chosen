FROM python:3.13-slim
RUN pip install --no-cache-dir aiohttp==3.14.5 \
 && useradd --system --uid 10001 --no-create-home relay \
 && mkdir /data && chown relay /data
COPY relay.py /app/relay.py
COPY static /app/static
USER relay
ENV PYTHONUNBUFFERED=1 PORT=8080 DATA_DIR=/data
EXPOSE 8080
CMD ["python", "/app/relay.py"]
