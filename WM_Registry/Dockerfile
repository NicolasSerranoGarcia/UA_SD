FROM python:3.12

WORKDIR /app
ENV DB_DIR=/var/lib/wm-registry

COPY pyproject.toml ./
COPY src/ ./src/
RUN python -m pip install --no-cache-dir '.[deployment]'

CMD ["sh", "-c", "exec gunicorn --bind \"0.0.0.0:${PORT:-8000}\" 'wm_registry.app:create_app()'"]
