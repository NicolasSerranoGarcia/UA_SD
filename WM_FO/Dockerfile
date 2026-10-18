FROM python:3.12

WORKDIR /app/WM_FO

COPY requirements.txt ./
RUN pip3 install --no-cache-dir -r requirements.txt

COPY . .

ENTRYPOINT ["python", "-u", "./WM_FO.py"]
    