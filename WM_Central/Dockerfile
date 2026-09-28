FROM python:3

WORKDIR /usr/src/water_management_app/core/central

COPY requirements.txt ./
RUN pip install --no-cache-dir -r requirements.txt

COPY . .

CMD ["python", "-u", "./WM_Central.py"]
