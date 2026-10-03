### *Is this dockerfile correct for the pass of arguments?*
```docker
FROM python:3

WORKDIR /usr/src/water_management_app/core/central

ENTRYPOINT ["python", "-u", "./WM_Central.py"]

COPY requirements.txt ./

RUN pip install --no-cache-dir -r requirements.txt

COPY . . 
```
