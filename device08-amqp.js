const { Mqtt: ProvisioningTransport } = require('azure-iot-provisioning-device-mqtt');
const { ProvisioningDeviceClient } = require('azure-iot-provisioning-device');
const { SymmetricKeySecurityClient } = require('azure-iot-security-symmetric-key');
const { Client, Message } = require('azure-iot-device');
const { Amqp } = require('azure-iot-device-amqp');

const idScope = '0ne010F6F29';
const deviceId = 'rzzzkx0v94';
const symmetricKey = 'Vbm1PJX0DkinlFKeRV8AYcCf5R0pQ0YLXOrnnfFv6vE=';
const modelId = 'dtmi:granjadecacao:SecadoYFermentacion_2pj;1';
const provisioningHost = 'global.azure-devices-provisioning.net';

async function main() {
  const securityClient = new SymmetricKeySecurityClient(deviceId, symmetricKey);
  const provisioningClient = ProvisioningDeviceClient.create(
    provisioningHost, idScope, new ProvisioningTransport(), securityClient
  );
  provisioningClient.setProvisioningPayload({ modelId });

  provisioningClient.register((err, result) => {
    if (err) {
      console.error('Error de aprovisionamiento:', err);
      return;
    }
    const connectionString =
      `HostName=${result.assignedHub};DeviceId=${result.deviceId};SharedAccessKey=${symmetricKey}`;

    const client = Client.fromConnectionString(connectionString, Amqp);

    client.open((err) => {
      if (err) {
        console.error('Error al conectar vía AMQP:', err);
        return;
      }
      console.log('Conectado a IoT Central vía AMQP (Node.js).');

      setInterval(() => {
        const payload = JSON.stringify({
          TempCaja: +(25 + Math.random() * 20).toFixed(2),
          humedadCaja: +(40 + Math.random() * 40).toFixed(2),
          masa: +(Math.random() * 15).toFixed(2),
        });
        const message = new Message(payload);
        client.sendEvent(message, (err) => {
          if (err) console.error('Error enviando:', err);
          else console.log('Enviado:', payload);
        });
      }, 60000);
    });
  });
}

main();