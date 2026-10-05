FROM python:3

WORKDIR /app/WM_Central

COPY requirements.txt ./
RUN pip install --no-cache-dir -r requirements.txt

COPY . .

ENTRYPOINT ["python", "-u", "./WM_Central.py"]
