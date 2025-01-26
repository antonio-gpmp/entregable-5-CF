# Proyecto: Distrito Telefónica de Madrid

Nuestro proyecto está dividido en tres subproyectos:
 - Gestión de Contenedores (Sensor de Basura)
 - Monitorización de Temperatura y Calidad del Aire (Sensor Temperatura)
 - Gestión de las Plazas del Parking (Gestión Parking)

Para poder probar cada uno de ellos tendrás que:
1. Abrir Wokwi.
2. Copiar el código del archivo sketch.ino en su respectiva pestaña en Wokwi.
3. Copiar el códido del archvio diagram.json en su respectiva pestaña en Wokwi.
4. Y añadir las librerías del libraries.txt mediante el Library Manager.

Si quieres modificar el servidor mqtt para poder utilizar el tuyo, tendrás que modificar estas 4 líneas del sketch.ino:
- const char* mqtt_server = ;
- const int mqtt_port = ;
- const char* mqtt_user = ;
- const char* mqtt_password = ;

Si quieres que los datos se guarden en tu base de datos de influxdb, lo primero que tendrás que hacer será crearte una 
cuenta en influxdb y generar un API Token y crear un bucket. Lo siguiente será tener telegraf instalado y 
modificar los datos del telegraf.config que te proporcionamos. Una vez lo tengas instalado tendrás que ejecutar el
siguiente comando desde el directorio donde tengas el telegraf: .\telegraf.exe --config <direccion_del_telegraf.conf>
